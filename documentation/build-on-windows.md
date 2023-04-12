# Building Nightingale on Windows 
## When to build Nightingale 
On Windows, you do never need to build Nightingale if you want to use the application. You can download binaries from the [homepage](http://getnightingale.com), wrapped into an easy-to-use installer. If you're interested in developing add-ons or feathers, you also do not need to build Nightingale. Install it the usual way and you're ready to go. For some complex add-ons the build system described here might be needed or helpful, through. But if you don't know about this you almost certainly do not want to get a build system. So, the only reason for building Nightingale is if you want to work on the core code of Nightingale, or if you want to use the Nightingale Build System for building add-ons (remember: most add-ons do NOT need to be built!). 

## If you're sure you want to build 
After we discussed when not to build, let's move to the actual building process: 

## Setting up a Build Environment 
You need some tools to build Nightingale. Here's a (maybe incomplete) list of requirements for using our build system: 
* [a Mozilla Build System](http://ftp.mozilla.org/pub/mozilla.org/mozilla/libraries/win32/MozillaBuildSetup-1.5.exe) (Version 1.5) with a supported compiler, as included in [Visual Studio 2008 Express](http://download.microsoft.com/download/E/8/E/E8EEB394-7F42-4963-A2D8-29559B738298/VS2008ExpressWithSP1ENUX1504728.iso ) (Thanks to [StackOverflow](http://stackoverflow.com/questions/4482159/where-can-i-download-visual-studio-express-2008-not-2010)) (this is not officially supported, but works well for me, and the debugger is great) 
* [the Windows SDK](http://msdn.microsoft.com/en-US/windows/ff851942.aspx) (for Windows 7 and .NET Framework 3.5 SP1) 
* [the Windows Driver Kit](http://www.microsoft.com/download/en/details.aspx?id=11800) 
* [Git for Windows](http://git-scm.com/) 
* [cwRsync](https://www.itefix.no/i2/sites/default/files/cwRsync_4.0.5_Installer.zip) (Cygwin rsync) if you want to use buildbot to produce nightlies (rsync.exe/ssh.exe) You can install most of these with the [chocolatey package manager](http://chocolatey.org) using this command: 

    choco install mozillabuild windows-sdk-7.0 git visualstudioexpress2008 


You need to set some environment variables for the build system to find all libraries. To prevent your system's variables from getting polluted, I recommend updating your start-msvcX.bat (see Building), edit the last lines to match (note: this is for WDK 7600.16385.1 and Visual Studio 2008, you may adjust the paths:

    echo Set WDK set INCLUDE=%INCLUDE%;c:\WinDDK\7600.16385.1\inc\api;c:\WinDDK\7600.16385.1\inc\crt;c:\WinDDK\7600.16385.1\inc\atl71 set LIB=%LIB%;c:\WinDDK\7600.16385.1\lib\wxp\i386;c:\WinDDK\7600.16385.1\lib\Crt\i386;C:\WinDDK\7600.16385.1\lib\ATL\i386 set PATH=%PATH%;c:\WinDDK\7600.16385.1\bin\x86\x86;c:\Program Files\Microsoft SDKs\Windows\v6.0A\Bin;c:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\bin;c:\mozilla-build\msys\bin;c:\mozilla-build\moztools\bin;C:\Program Files (x86)\Microsoft Visual Studio 8\VC\bin;C:\Program Files (x86)\git\bin "%MOZILLABUILD%\msys\bin\bash" --login -i
* You need to delete (or rename) the vim/vim72/install.exe within mozilla-build, as it blocks the correct install.exe from being accessed through the build system. 

## Configure the build
You can specify options in nightingale.config. The file does not exist by default, as it is ignored by .gitignore. Example nightingale.config for building on Windows: 

    ac_add_options --disable-compiler-environment-checks ac_add_options --with-media-core=default ac_add_options --enable-installer ac_add_options --with-msvc-express 
    
This disables checking the compiler (so it works with VC9 / Visual Studio 2008), sets the default media core (just do it in case you don't know what this is, in theory this is also the default value but there were some issues in the past...) and enables building an installer. You can specify the following flags in nightingale.config (prefix with ac_add_options): 

    --enable-debug whether or not to compile a debug version (disabled by default) --enable-jars whether or not to create jar files (enabled by default) --enable-installer whether or not to create installer (disabled by default) --enable-official whether or not to create an official build (disabled by default) --enable-nightly whether or not to create an nightly build (disabled by default) --enable-tests whether or not to build tests (defaults: debug:enabled rel:disabled) --enable-update-channel=CHANNEL which update channel to use ("default") --enable-breakpad whether or not to enable breakpad (disabled by default in non-official builds) --enable-static experimental single-component static build support (disabled by default) --enable-compiler-environment-checks only allow officially-supported compiler versions (enabled by default) 
    
Please note that installer building needs quite much free RAM (~1.5 GB currently) and will fail if that amount is not available. 

## Additional steps for debug builds 
No additional steps should be required. Just be sure to add the debug flag to your build config and build with build.sh. 

## Building 
After you set everything up, you can open a console for building by the batch script for your VC-Version (Visual Studio 2008 is VC9 -> start-msvc9.bat). Do not use x64-scripts, even if you're on x64 Windows! The following assumes you have a buildable Nightingale source branch in C:\Nightingale. Type the following to build: 

    cd /c/Nightingale build.sh

## Running a successful build 
If everything succeeded, you have a Nightingale built in C:\Nightingale\compiled\dist. If you choose to build installers, they are in C:\Nightingale\compiled\_built_installer. 

## Troubleshooting 
 If you get the following : 
 
    /c/nightingale-hacking/configure: line 8200: test: : integer expression expected /c/nightingale-hacking/configure: line 8213: test: : integer expression expected configure: error: The linker major version, 9.00.30729.01, does not match the compiler suite version, . make: *** [/c/nightingale-hacking/compiled/config.status] Error 1 
    
Edit configure.ac file, search for line that contains "CC_VERSION=" and replaced it by the version of "cl" : 
    
    # Determine compiler version # CC_VERSION=`"${CC}" -v 2>&1 | sed -ne "$_MSVC_VER_FILTER"` CC_VERSION="15.00.30729.01" 
    
It should compile fine now. It should be [fixed](https://github.com/nightingale-media-player/nightingale-hacking/commit/93d2f7761957df0243b3decf5127ea5d0fec537c) as of 07/03/2014. For other issues feel free to in IRC or the forums! 