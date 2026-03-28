This repository contains an implementation of the paper [A Low-Dimensional
Function Space for Efficient Spectral
Upsampling](http://rgl.epfl.ch/publications/Jakob2019Spectral) by Wenzel Jakob
and Johannes Hanika.

In comparison to the supplemental material of the original paper that optimized
polynomial coefficients using Google's CEPHES solver, the code here relies on a
much simpler and self-contained Gauss-Newton solver. Mitsuba and PBRT use a
variant of this code that is simply executed as part of the CMake build system.
