#/bin/bash

echo "Evaluating Test Cases on VM";
cd /home/aqualab/.aqualab/project3;
make test-reg SHELL_ARCH=64;
rm -rf /home/aqualab/.aqualab/project3;
mkdir /home/aqualab/.aqualab/project3;
