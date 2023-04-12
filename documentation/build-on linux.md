# Building Nightingale on Linux

## When to build Nightingale

On Linux, you can either download the binaries from the [homepage](http://getnightingale.com) or build the software yourself. If you're interested in developing add-ons or feathers, you do not necessarily need to build Nightingale. For some complex add-ons the build system described here might be needed or helpful, through. Building Nightingale allow you to work on the core code of Nightingale and also allow you to use the Nightingale Build System for building add-ons (Remember: most add-ons do NOT need to be built!). ===== If you're sure you want to build 

## Let's move to the actual building process:

### Setting up a Build Environment 
You need some tools to build Nightingale. Here's a (maybe incomplete) list of requirements for using our build system: 

#### Ubuntu 14.04.1 
    sudo apt-get install git autoconf g++ libgtk2.0-dev libdbus-glib-1-dev libtag1-dev libgstreamer-plugins-base0.10-dev zip unzip libidl-dev libasound2-dev 
#### Fedora 18
    sudo yum -y install autoconf git-all gcc-c++ gtk2-devel dbus-glib-devel gstreamer-plugins-base-devel 

#### OpenSUSE 13.2
    zypper install git autoconf gcc-c++ gtk2-devel dbus-1-glib-devel libtag-devel gstreamer-plugins-base-devel gstreamer-0_10-plugins-base-devel libidl-devel zip unzip 

### Clone the Git Repository 
    git clone https://github.com/nightingale-media-player/nightingale-hacking.git

To build a trunk build (like a nightly): git checkout sb-trunk-oldxul 
If you want to build from a past release type the following, replacing VERSION with the desired version number: git checkout nightingale-VERSION 

### Configure the build 
You can specify options in nightingale.config. The file does not exist by default, as it is ignored by .gitignore. You can specify the following flags in nightingale.config (prefix with ac_add_options):

    --enable-debug whether or not to compile a debug version (disabled by default) --enable-jars whether or not to create jar files (enabled by default) --enable-official whether or not to create an official build (disabled by default) --enable-nightly whether or not to create an nightly build (disabled by default) --enable-tests whether or not to build tests (defaults: debug:enabled rel:disabled) --enable-update-channel=CHANNEL which update channel to use ("default") --enable-breakpad whether or not to enable breakpad (disabled by default in non-official builds) --enable-static experimental single-component static build support (disabled by default) --enable-compiler-environment-checks only allow officially-supported compiler versions (enabled by default) --enable-unity-integration enables Unity integration for audio menus and notifications (disabled by default) --enable-system-nspr enables usage of your system's NSPR instead of a bundled version (disabled by default) --enable-metrics enables integration of metrics collection infrastructure (disabled by default)

### Additional Steps to build a Debian Package 
For Building, please skip to the Debian Package part of the Building section. 

### Aditional dependencies 
Building nightingale as a debian package brings additional dependencies with itself. To install the type 

    sudo apt-get install build-essentials libgnomevfs2-dev libsqlite0-dev libnspr4-dev sqlite 
    
is in theory not always needed, but it is listed in the debian/control file. 

#### Prepare the directory
In order to build the debian package, you can choose between two options: Change debian/source/format to ''3.0 (native)'' which in theory is wrong. Alternatively you can put a tarball of the source (you can use your local source or [download](https://github.com/nightingale-media-player/nightingale-hacking/tarball/sb-trunk-oldxul) the tarball from GitHub named nightingale_{version}.orig.tar.gz, where {version} is the latest version number according to the changelog in the debian directory up to the dash (e.g. 1.12.2), into the root nightingale source directory. 

### Additional steps for debug builds 
No special additional steps are required - apart from adding the debug build flag. build.sh should handle everything else for you.

### Building 
After you set everything up, you can open a terminal for building. Type the following to build: <code> ./build.sh </code> If everything is successful, you should see the words: "Build Successful!" on the last line of the output. 

### Building a Debian Package 
Navigate to the nightingale source code directory containing the "debian" directory in a terminal. Type 

    dpkg-buildpackage 
        
If it's only a local build and you don't want to sign the .dsc and .changes file, type 

    dpkg-buildpackage -uc -us 

### Running a successful build 
To run after the build succeeded, run <code> make run </code> Alternatively you can find the binary and resources in the compiled/dist folder relative to your source directory. For instructions on how to install a self-built Nightingale, see the INSTALL file in the source directory. 

### Troubleshooting 
 **TODO** If you are getting taglib reference errors when trying to build, you may need to manually install TagLib 1.8. See this [forum post](http://forum.getnightingale.com/thread-617-post-2932.html#pid2932) for details. 
 ### External Resources 
 [Build Thread](http://forum.getnightingale.com/thread-28.html) 