Junction is a library of concurrent data structures in C++. It contains three hash map implementations:

    junction::ConcurrentMap_Linear
    junction::ConcurrentMap_LeapFrog
    junction::ConcurrentMap_Grampa

[CMake](https://cmake.org/) and [Turf](https://github.com/preshing/turf) are required. See the blog post [New Concurrent Hash Maps for C++](http://preshing.com/20160201/new-concurrent-hash-maps-for-cpp/) for more information.

## License

Junction uses the Simplified BSD License. You can use the source code freely in any project, including commercial applications, as long as you give credit by publishing the contents of the `LICENSE` file in your documentation somewhere.

## Getting Started

If you just want to get the code and look around, start by cloning Junction and Turf into adjacent folders, then run CMake on Junction's `CMakeLists.txt`. You'll want to pass different arguments to `cmake` depending on your platform and IDE.

    $ git clone https://github.com/preshing/junction.git
    $ git clone https://github.com/preshing/turf.git
    $ cd junction
    $ mkdir build
    $ cd build
    $ cmake <additional options> ..

On Unix-like environments, `cmake` will generate a Makefile by default. On Windows, it will create a Visual Studio solution. To use a specific version of Visual Studio:

    $ cmake -G "Visual Studio 14 2015" ..

To generate an Xcode project on OS X:

    $ cmake -G "Xcode" ..

To generate an Xcode project for iOS:

    $ cmake -G "Xcode" -DCMAKE_TOOLCHAIN_FILE=../../turf/cmake/toolchains/iOS.cmake ..

The generated build system will contain separate targets for Junction, Turf, and some sample applications.

![Solution Explorer](/docs/vs-solution.png)

Alternatively, you can run CMake on a specific sample only:

    $ cd junction/samples/MapCorrectnessTests
    $ mkdir build
    $ cd build
    $ cmake <additional options> ..

## Adding Junction to Your Project

There are several ways to add Junction to your own C++ project.

1. Add Junction as a build target in an existing CMake-based project.
2. Use CMake to build Junction and Turf, then link the static libraries into your own project.
3. Grab only the source files you need from Junction, copy them to your project and hack them until they build correctly.

Some developers will prefer approach #3, but I encourage you to try approach #1 or #2 instead. It will be easier to grab future updates that way. There are plenty of files in Junction (and Turf) that you don't really need, but it won't hurt to keep them on your hard drive either. And if you link Junction statically, the linker will exclude the parts that aren't used.

### Adding to an Existing CMake Project

If your project is already based on CMake, clone the Junction and Turf source trees somewhere, then call `add_subdirectory` on Junction's root folder from your own CMake script. This will add both Junction and Turf targets to your build system.

If you use Git, you can add the Junction and Turf repositories as submodules. Otherwise, you can just copy the Junction and Turf source trees to your repository.

[FIXME: Create a repository with a sample project that demonstrates this.]

### Building the Libraries Separately

Generate Junction's build system using the steps described in the *Getting Started* section, then use it to build the libraries you need. Add these to your own build system. Make sure to generate static libraries to avoid linking parts of the library that aren't needed.

[FIXME: Use CMake's install feature to generate a clean output tree, so users don't have to fiddle with include and library paths too much.]

## Configuration

When you first run CMake on Junction, Turf will detect the capabilities of your compiler and write the results to a file in the build tree named `include/turf_config.h`. Similarly, Junction will write `include/junction_config.h` to the build tree. You can modify the contents of those files by setting variables when CMake runs. This can be done by passing additional options to `cmake`, or by using an interactive GUI such as `cmake-gui` or `ccmake`.

For example, to configure Turf to use the C++11 standard library, you can set the `TURF_PREFER_CPP11` variable on the command line:

    $ cmake -DTURF_PREFER_CPP11=1 ..

Or, using the CMake GUI:

![CMake GUI](/docs/cmake-gui.png)

Many header files in Turf, and some in Junction, are configurable using preprocessor definitions. For example, `turf/Thread.h` will switch between `turf::Thread` implementations depending on the values of `TURF_IMPL_THREAD_PATH` and `TURF_IMPL_THREAD_TYPE`. If those macros are not defined, they will be set to default values based on information from the environment. You can set them directly by providing your own header file and passing it in the `TURF_USERCONFIG` variable when CMake runs. You can place this file anywhere; CMake will copy it to Turf's build tree right next to `include/turf_config.h`.

    $ cmake -DTURF_USERCONFIG=path/to/custom/turf_userconfig.h.in ..

The `JUNCTION_USERCONFIG` variable works in a similar way. As an example, take a look at the Python script `junction/samples/MapScalabilityTests/TestAllMaps.py`. This script invokes `cmake` several times, passing a different `junction_userconfig.h.in` file each time. That's how it builds the same test application using different map implementations.

## Feedback

If you have any feedback on improving these steps, feel free to [open an issue](https://github.com/preshing/junction/issues) on GitHub, or send a direct message using the [contact form](http://preshing.com/contact/) on my blog.
