#!/bin/bash

unameOut="$(uname -s)"

echo "Running on ${unameOut} as ${USER}"

case "${unameOut}" in
    Linux*)
        echo "Installing Linux dependencies"
        sudo script/install_debian_dependencies.sh
        ;;

    Darwin*)
        echo "Installing MacOS dependencies"
        script/install_macos_dependencies.sh
        ;;

    *)
        echo "Error: Machine not supported"
        exit -1
esac
