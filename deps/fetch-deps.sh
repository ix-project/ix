#!/bin/sh

git_clone_checkout() {
  git clone "$1" "$2"
  git -C "$2" checkout "$3"
}

DIR=`dirname $0`
git_clone_checkout git://dpdk.org/dpdk $DIR/dpdk 82fb702077f67585d64a07de0080e5cb6a924a72
git_clone_checkout https://github.com/epfl-dcsl/dune.git $DIR/dune 3b062bf84da8944c1c73b65bf5afa727318dec57


