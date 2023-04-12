# Building Nightingale on Mac OS X
## When to build Nightingale 
On OS X, you can either download the binaries from the [homepage](http://getnightingale.com), get the [nightlies](http://sourceforge.net/projects/ngale/files/trunk-Nightlies/), or build the software yourself. If you're interested in developing add-ons or feathers, you do not necessarily need to build Nightingale. For some complex add-ons the build system described here might be needed or helpful, through. Building Nightingale allows you to work on the core code of Nightingale and also allows you to use the Nightingale Build System for building add-ons (Remember: most add-ons do NOT need to be built!). 

## Setting up a Build Environment 
Our build machine, an Intel Mac Mini, is currently running OS X 10.5.8, with Xcode 3.1.4 installed. Until we move on to a newer version of some of our dependencies, you MUST have the 10.5 SDK. If you plan on building the dependencies repo, you must also have an Apple-provided version of gcc-4.2 installed. To install these on versions beyond 10.6.8, you will need to download an older version of Xcode from the Apple developers site and install it alongside any other versions of Xcode you currently have installed. You will also need to install Macports if you have not already. From there, install the following packages (open a terminal window and enter the commands given in brackets): *GNU make 3.80 or newer. (sudo port install gmake) *GNU autoconf v2.59 or newer. (sudo port install autoconf) *GNU coreutils. (sudo port install coreutils) 

* GNU gcc 4.7 (sudo port install gcc47) - OPTIONAL, but produces better binaries when building the deps. All except for those that expicitly use gcc-4.2 work with gcc-mp-4.7. We have relinked our compiler links in /usr/bin to point to gcc-mp-4.7 and g++-mp-4.7. 
* libIDL (sudo port install libidl) - Only required for the dependencies repo 
* autoconf 2.13 (sudo port install autoconf213) - Only required for the dependencies repo 
* cmake - (sudo port install cmake) - Only required for the dependencies, see the Hacks section for a tweak it requires. 

## Clone the Git Repository 

    git clone https://github.com/nightingale-media-player/nightingale-hacking.git 
    cd nightingale-hacking 
    git checkout sb-trunk-oldxul 

## Configure the build 
You can specify options in nightingale.config. The file does not exist by default, as it is ignored by .gitignore. You can specify the following flags in nightingale.config (prefix with ac_add_options):

    --enable-debug whether or not to compile a debug version (disabled by default) --enable-jars whether or not to create jar files (enabled by default) --enable-official whether or not to create an official build (disabled by default) --enable-nightly whether or not to create an nightly build (disabled by default) --enable-tests whether or not to build tests (defaults: debug:enabled rel:disabled) --enable-update-channel=CHANNEL which update channel to use ("default") --enable-breakpad whether or not to enable breakpad (disabled by default in non-official builds) --enable-static experimental single-component static build support (disabled by default) --enable-compiler-environment-checks only allow officially-supported compiler versions (enabled by default) 
    
## Additional steps for debug builds
You only have to add ac_add_options --enable-debug to your nightingale.config before building with build.sh to create a debug build.

## Building
Type the following to build: <code> ./build.sh </code> If everything is successful, you should see the words: "Build Successful!" on the last line of the output. 

## Running a successful build 
You should have a Nightingale.app built in compiled/dist and a .dmg installer in compiled/_built_installer. 
## Troubleshooting
**Issue**: cmake gives an error when building on 10.5.8 and possibly other versions: 

**Solution**: Edit the /usr/share/cmake-{version}/modules/platform/Darwin.cmake file and remove an if/else conditional that didn’t like that an SDK newer than 10.4u is being used. Remove the entire if/else block that contains the error message 
   
    "CMAKE_OSX_DEPLOYMENT_TARGET (${_deploy}) is greater than CMAKE_OSX_SYSROOT SDK (${_sdk_path}). Please set CMAKE_OSX_DEPLOYMENT_TARGET to ${SDK} or lower" 

## External Resources
[Mac Build Thread](http://forum.getnightingale.com/thread-429.html) 