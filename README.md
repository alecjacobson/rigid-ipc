# Rigid IPC

**Robust, intersection-free, simulations of rigid bodies in 2D and 3D.**

## Compilation

To build the project, use the following commands from the root directory of the project:

```bash
mkdir build
cd build
cmake ..
make -j4
```

### Dependencies

Most dependancies are downloaded through CMake depending on the build options.
The only exceptions to this are:

* [Boost](https://www.boost.org/): We currently use the interval arithmetic
library for interval root finding and the filesystem library for path
manipulation.

## Scenes

We take as input a single JSON file that specifies the mesh and initial
conditions for each body. The `fixtures` directory contains example scenes.

## Python Bindings

We expose some functionality of Rigid IPC through Python. This is still in
development and lacks the ability to script many features available in the full
simulator.

To build the Python bindings use the `setup.py` script:
```sh
python setup.py install
```
