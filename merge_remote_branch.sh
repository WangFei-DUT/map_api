#!/bin/bash

if [ -z "$1" ]
  then
    echo "No argument for the repository supplied. Usage: pull_branch.sh reposity_name branch_name"
fi
if [ -z "$2" ]
  then
    echo "No argument for the branch supplied. Usage: pull_branch.sh reposity_name branch_name"
fi

branch_name=$(git symbolic-ref -q HEAD)
branch_name=${branch_name##refs/heads/}
branch_name=${branch_name:-HEAD}

echo -e "\e[1;31mThis will merge \"ethz-asl_$1 $2\" into your local branch \"$branch_name\"! \e[0m"

while true; do
    read -p "Continue? [y/n]" yn
    case $yn in
        [Yy]* ) git fetch ethz-asl_$1 $2 && git subtree pull --prefix $1 ethz-asl_$1 $2; break;;
        [Nn]* ) exit;;
        * ) echo "Please answer yes or no.";;
    esac
done



