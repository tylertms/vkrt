#include "../src/shared/ggx_energy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>
#include <zlib.h>

struct Direction {
    double x, y, z;
};

static constexpr uint32_t sampleCount = 4096u;
static constexpr uint32_t criticalSampleCount = 16384u;
static std::array<Direction, sampleCount> sampleCoordinates;
static std::array<Direction, criticalSampleCount / 2u> criticalSampleCoordinates;

static double radicalInverse(uint32_t value) {
    value = ((value & 0x55555555u) << 1u) | ((value >> 1u) & 0x55555555u);
    value = ((value & 0x33333333u) << 2u) | ((value >> 2u) & 0x33333333u);
    value = ((value & 0x0f0f0f0fu) << 4u) | ((value >> 4u) & 0x0f0f0f0fu);
    value = ((value & 0x00ff00ffu) << 8u) | ((value >> 8u) & 0x00ff00ffu);
    value = (value << 16u) | (value >> 16u);
    return (double(value) + 0.5) / 4294967296.0;
}

static Direction normalize(Direction v) {
    double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / length, v.y / length, v.z / length};
}

static double fresnel(double cosine, double eta) {
    double sinSquared = (1.0 - cosine * cosine) / (eta * eta);
    if (sinSquared >= 1.0) return 1.0;
    double transmittedCosine = std::sqrt(1.0 - sinSquared);
    double parallel = (eta * cosine - transmittedCosine) / (eta * cosine + transmittedCosine);
    double perpendicular = (cosine - eta * transmittedCosine) / (cosine + eta * transmittedCosine);
    return 0.5 * (parallel * parallel + perpendicular * perpendicular);
}

static double escapeWeight(
    Direction wi,
    double outgoingCosine,
    double projectedOutgoing,
    double alphaX,
    double alphaY
) {
    double cosine = std::abs(wi.z);
    double projectedIncoming =
        std::sqrt(alphaX * alphaX * wi.x * wi.x + alphaY * alphaY * wi.y * wi.y + cosine * cosine);
    return cosine * (outgoingCosine + projectedOutgoing) /
           (outgoingCosine * projectedIncoming + cosine * projectedOutgoing);
}

static double outgoingCosine(uint32_t index) {
    if (index < VKRT_GGX_ENERGY_GRAZING_SIZE) {
        return std::exp2(-24.0 + 21.0 * double(index) / (VKRT_GGX_ENERGY_GRAZING_SIZE - 1u));
    }
    double t = double(index - (VKRT_GGX_ENERGY_GRAZING_SIZE - 1u)) /
               (VKRT_GGX_ENERGY_COSINE_SIZE - VKRT_GGX_ENERGY_GRAZING_SIZE);
    double root = std::sqrt(0.125) + (1.0 - std::sqrt(0.125)) * t;
    return root * root;
}

static void integrate(uint32_t index, std::vector<float>& table) {
    constexpr double pi = 3.14159265358979323846;
    bool glass = index >= VKRT_GGX_ENERGY_REFLECTION_SIZE;
    uint32_t cell = glass ? index - VKRT_GGX_ENERGY_REFLECTION_SIZE : index;
    uint32_t roughnessIndex = cell % VKRT_GGX_ENERGY_ROUGHNESS_SIZE;
    uint32_t cosineIndex = (cell / VKRT_GGX_ENERGY_ROUGHNESS_SIZE) % VKRT_GGX_ENERGY_COSINE_SIZE;
    uint32_t iorIndex = (cell / VKRT_GGX_ENERGY_REFLECTION_SIZE) % VKRT_GGX_ENERGY_IOR_SIZE;
    bool interior = cell >= VKRT_GGX_ENERGY_GLASS_SIZE;
    uint32_t anisotropyIndex = (cell / VKRT_GGX_ENERGY_ANGULAR_SIZE) % VKRT_GGX_ENERGY_ANISOTROPY_SIZE;
    uint32_t azimuthIndex =
        (cell / (VKRT_GGX_ENERGY_ANGULAR_SIZE * VKRT_GGX_ENERGY_ANISOTROPY_SIZE)) % VKRT_GGX_ENERGY_AZIMUTH_SIZE;
    double roughness = double(roughnessIndex) / (VKRT_GGX_ENERGY_ROUGHNESS_SIZE - 1u);
    double aspect = 1.0 - (1.0 - std::sqrt(0.1)) * double(anisotropyIndex) / (VKRT_GGX_ENERGY_ANISOTROPY_SIZE - 1u);
    double alphaX = std::max(roughness * roughness / aspect, 0.001);
    double alphaY = std::max(roughness * roughness * aspect, 0.001);
    double azimuth = 0.5 * pi * double(azimuthIndex) / (VKRT_GGX_ENERGY_AZIMUTH_SIZE - 1u);
    double cosine = outgoingCosine(cosineIndex);
    double iorCoordinate = double(iorIndex) / (VKRT_GGX_ENERGY_IOR_SIZE - 1u);
    double eta = 1.0 + std::exp2(-23.0 + 33.0 * iorCoordinate);
    if (interior) eta = 1.0 / eta;
    Direction wo =
        {std::sqrt(1.0 - cosine * cosine) * std::cos(azimuth),
         std::sqrt(1.0 - cosine * cosine) * std::sin(azimuth),
         cosine};
    double projectedOutgoing =
        std::sqrt(alphaX * alphaX * wo.x * wo.x + alphaY * alphaY * wo.y * wo.y + cosine * cosine);
    Direction stretched = normalize({alphaX * wo.x, alphaY * wo.y, cosine});
    double energy = 0.0;
    double schlickEnergy = 0.0;
    bool critical = glass && eta > 0.95 && eta < 1.0 && cosine < 0.25;
    uint32_t integrationSamples = critical ? criticalSampleCount : sampleCount;
    double criticalLimit = critical ? std::min(4.0 * std::sqrt(1.0 - eta * eta), 1.0) : 0.0;
    double xyLength = std::hypot(wo.x, wo.y);
    Direction tangent = xyLength > 0.0 ? Direction{-wo.z * wo.x / xyLength, -wo.z * wo.y / xyLength, xyLength}
                                       : Direction{1.0, 0.0, 0.0};
    Direction bitangent =
        {wo.y * tangent.z - wo.z * tangent.y, wo.z * tangent.x - wo.x * tangent.z, wo.x * tangent.y - wo.y * tangent.x};
    auto azimuthLimit = [&](double t) {
        double denominator = std::sqrt(std::max((1.0 - t * t) * (1.0 - wo.z * wo.z), 0.0));
        return denominator > 0.0 ? std::acos(std::clamp(-t * wo.z / denominator, -1.0, 1.0)) : pi;
    };
    for (uint32_t sample = 0u; sample < integrationSamples; sample++) {
        uint32_t sequence = critical ? sample % (integrationSamples / 2u) : sample;
        double sequenceX = (double(sequence) + 0.5) / (integrationSamples / 2u);
        double sequenceY = critical ? criticalSampleCoordinates[sequence].z : sampleCoordinates[sample].z;
        Direction u = critical ? criticalSampleCoordinates[sequence] : sampleCoordinates[sample];
        double z = (1.0 - u.z) * (1.0 + stretched.z) - stretched.z;
        double radius = std::sqrt(std::max(1.0 - z * z, 0.0));
        Direction m =
            normalize({alphaX * (stretched.x + radius * u.x), alphaY * (stretched.y + radius * u.y), stretched.z + z});
        if (critical && sample >= integrationSamples / 2u) {
            double t = criticalLimit * sequenceY;
            double phi = (2.0 * sequenceX - 1.0) * azimuthLimit(t);
            double radius = std::sqrt(std::max(1.0 - t * t, 0.0));
            m =
                {t * wo.x + radius * (std::cos(phi) * tangent.x + std::sin(phi) * bitangent.x),
                 t * wo.y + radius * (std::cos(phi) * tangent.y + std::sin(phi) * bitangent.y),
                 t * wo.z + radius * (std::cos(phi) * tangent.z + std::sin(phi) * bitangent.z)};
        }
        if (m.z <= 0.0) continue;
        double dotWoM = std::clamp(wo.x * m.x + wo.y * m.y + wo.z * m.z, 0.0, 1.0);
        double densityRatio = 1.0;
        if (critical) {
            double normSquared = m.x * m.x / (alphaX * alphaX) + m.y * m.y / (alphaY * alphaY) + m.z * m.z;
            double visiblePdf =
                2.0 * dotWoM / (pi * alphaX * alphaY * normSquared * normSquared * (cosine + projectedOutgoing));
            double criticalPdf = dotWoM < criticalLimit ? 1.0 / (2.0 * criticalLimit * azimuthLimit(dotWoM)) : 0.0;
            densityRatio = visiblePdf > 0.0 ? 2.0 * visiblePdf / (visiblePdf + criticalPdf) : 0.0;
        }
        Direction reflectedDirection =
            {2.0 * dotWoM * m.x - wo.x, 2.0 * dotWoM * m.y - wo.y, 2.0 * dotWoM * m.z - cosine};
        double reflectionCosine = reflectedDirection.z;
        double reflected =
            reflectionCosine > 0.0 ? escapeWeight(reflectedDirection, cosine, projectedOutgoing, alphaX, alphaY) : 0.0;
        if (!glass) {
            energy += reflected;
            schlickEnergy += reflected * std::pow(1.0 - dotWoM, 5.0);
            continue;
        }
        double F = fresnel(dotWoM, eta);
        energy += densityRatio * F * reflected;
        if (F >= 1.0) continue;
        double transmittedMicroCosine = std::sqrt(std::max(1.0 - (1.0 - dotWoM * dotWoM) / (eta * eta), 0.0));
        double transmissionCosine = cosine / eta - (dotWoM / eta - transmittedMicroCosine) * m.z;
        if (transmissionCosine > 0.0) {
            Direction transmittedDirection =
                {-wo.x / eta + (dotWoM / eta - transmittedMicroCosine) * m.x,
                 -wo.y / eta + (dotWoM / eta - transmittedMicroCosine) * m.y,
                 -transmissionCosine};
            energy += densityRatio * (1.0 - F) *
                      escapeWeight(transmittedDirection, cosine, projectedOutgoing, alphaX, alphaY);
        }
    }
    if (glass) {
        table[VKRT_GGX_ENERGY_GLASS_OFFSET + cell] = float(std::min(energy / integrationSamples, 1.0));
    } else {
        table[2u * cell] = float(std::min(energy / integrationSamples, 1.0));
        table[2u * cell + 1u] = float(schlickEnergy / sampleCount);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: generate_ggx_energy output.coeff\n");
        return 1;
    }
    for (uint32_t sample = 0u; sample < sampleCount; sample++) {
        double phi = 6.28318530717958647692 * (double(sample) + 0.5) / sampleCount;
        sampleCoordinates[sample] = {std::cos(phi), std::sin(phi), radicalInverse(sample)};
    }
    for (uint32_t sample = 0u; sample < criticalSampleCoordinates.size(); sample++) {
        double phi = 6.28318530717958647692 * (double(sample) + 0.5) / criticalSampleCoordinates.size();
        criticalSampleCoordinates[sample] = {std::cos(phi), std::sin(phi), radicalInverse(sample)};
    }
    std::vector<float> table(VKRT_GGX_ENERGY_COUNT);
    std::atomic<uint32_t> nextCell{0u};
    std::vector<std::thread> workers;
    uint32_t cellCount = VKRT_GGX_ENERGY_REFLECTION_SIZE + 2u * VKRT_GGX_ENERGY_GLASS_SIZE;
    for (uint32_t i = 0u; i < std::max(std::thread::hardware_concurrency(), 1u); i++) {
        workers.emplace_back([&] {
            for (;;) {
                uint32_t cell = nextCell.fetch_add(1u);
                if (cell >= cellCount) break;
                integrate(cell, table);
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    for (uint32_t anisotropy = 0u; anisotropy < VKRT_GGX_ENERGY_ANISOTROPY_SIZE; anisotropy++) {
        for (uint32_t roughness = 0u; roughness < VKRT_GGX_ENERGY_ROUGHNESS_SIZE; roughness++) {
            double average = 0.0;
            for (uint32_t phi = 0u; phi < VKRT_GGX_ENERGY_AZIMUTH_SIZE; phi++) {
                uint32_t offset = (phi * VKRT_GGX_ENERGY_ANISOTROPY_SIZE + anisotropy) * VKRT_GGX_ENERGY_ANGULAR_SIZE;
                double phiWeight = (phi == 0u || phi + 1u == VKRT_GGX_ENERGY_AZIMUTH_SIZE ? 0.5 : 1.0) /
                                   (VKRT_GGX_ENERGY_AZIMUTH_SIZE - 1u);
                for (uint32_t mu = 0u; mu + 1u < VKRT_GGX_ENERGY_COSINE_SIZE; mu++) {
                    double x0 = outgoingCosine(mu);
                    double x1 = outgoingCosine(mu + 1u);
                    double e0 = table[2u * (offset + mu * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + roughness)];
                    double e1 = table[2u * (offset + (mu + 1u) * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + roughness)];
                    if (mu + 1u < VKRT_GGX_ENERGY_GRAZING_SIZE) {
                        double slope = (e1 - e0) / std::log(x1 / x0);
                        average += phiWeight * (x1 * x1 * (e1 - 0.5 * slope) - x0 * x0 * (e0 - 0.5 * slope));
                    } else {
                        double t0 = std::sqrt(x0), t1 = std::sqrt(x1);
                        double slope = (e1 - e0) / (t1 - t0);
                        average += phiWeight * ((e0 - slope * t0) * (x1 * x1 - x0 * x0) +
                                                0.8 * slope * (x1 * x1 * t1 - x0 * x0 * t0));
                    }
                }
            }
            table[VKRT_GGX_ENERGY_AVERAGE_OFFSET + anisotropy * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + roughness] =
                float(average);
        }
    }
    std::vector<uint8_t> payload;
    payload.reserve(table.size() * sizeof(uint16_t));
    for (float value : table) {
        if (!std::isfinite(value) || value < 0.0f || value > 1.00001f) return 1;
        int exponent;
        std::frexp(value, &exponent);
        int shift = std::max(exponent - 1, -14);
        uint32_t mantissa = uint32_t(std::round(std::ldexp(double(value), 10 - shift)));
        uint16_t bits =
            value == 0.0f ? 0u : uint16_t(mantissa + (shift > -14 || mantissa >= 1024u ? (shift + 14) * 1024u : 0u));
        payload.push_back(uint8_t(bits));
        payload.push_back(uint8_t(bits >> 8u));
    }
    uLongf compressedSize = compressBound(uLong(payload.size()));
    std::vector<uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, payload.data(), uLong(payload.size()), Z_BEST_COMPRESSION) !=
        Z_OK)
        return 1;
    FILE* output = std::fopen(argv[1], "wb");
    if (!output) return 1;
    bool valid = std::fwrite(compressed.data(), 1u, compressedSize, output) == compressedSize;
    if (std::fclose(output) != 0) valid = false;
    return valid ? 0 : 1;
}
