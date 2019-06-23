Installation
============

To setup OpenSCADA, grpc and bazel package manager need to be first installed. To install them follow the steps given below:

Installing GRPC
^^^^^^^^^^^^^^^
* Install pip::
	sudo apt-get install python3-pip
* To install grpc for python execute the following commands::
	pip3 instatll grpcio grpcio-tools

Installing Bazel
^^^^^^^^^^^^^^^^

* Install OpenJdk::
	sudo apt-get install openjdk-8-jdk
* Install Bazel::
        echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" | sudo tee /etc/apt/sources.list.d/bazel.list
        curl https://bazel.build/bazel-release.pub.gpg | sudo apt-key add -
        sudo apt-get update && sudo apt-get install bazel
        sudo apt-get install --only-upgrade bazel

Make sure the version of bazel is atleast 0.21.0 or greater (run command: bazel version)

Installing OpenSCADA
^^^^^^^^^^^^^^^^^^^^

* Clone the git repository to $HOME directory::
	cd ~/ && git clone https://github.com/Vignesh2208/OpenSCADA.git && cd OpenSCADA

* Run the installation setup script. This would take a while the first time it is run because bazel downloads and installs other dependencies::
	sudo ./setup.sh install

Uninstalling OpenSCADA
^^^^^^^^^^^^^^^^^^^^^^

* To uninstall/cleanup run the following command::
	sudo ./setup.sh uninstall
	