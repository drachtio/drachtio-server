drachtio server for Enterprise Linux
==================================

To build the RPMs you need all of the packages listed in the "Building from source" section plus:

- *redhat-rpm-config*
- *rpm-build*

To build the RPMs:
- Checkout (clone) the Git repository
  `git clone --depth=50 --branch=main git://github.com/davehorton/drachtio-server.git` 
- Change the working directory
  `cd drachtio-server` 
- Run the below command to update Git submodules
  `git submodule update --init --recursive`
- Remove extra SPEC files(we must have just one SPEC file within the project to run rpmbuild)
  `find ./ -type f -name "*.spec" ! -name 'drachtio.spec' -delete`
- Create a tar archive
  `cd .. && tar -czvf drachtio.tar.gz drachtio-server/`
- Build the RPMs. For example,
   `rpmbuild -ta drachtio.tar.gz`

Once the build has completed the binary RPMs will be in `~/rpmbuild/RPMS`.