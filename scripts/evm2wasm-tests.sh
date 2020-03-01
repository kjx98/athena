#!/usr/bin/env bash

set -e

if [ "$1" == "build" ]
then
(
  git clone --recursive https://github.com/ethereum/cpp-ethereum
  cd cpp-ethereum

  rm -rf athena
  ln -s `pwd`/../. athena

  mkdir build
  cd build
  cmake -DATHENA=ON ..
  make
)
  TESTETH=$(pwd)/cpp-ethereum/build/test/testeth
else
  TESTETH=testeth
fi

WORKING_DIR=$(pwd)
echo "running tests.sh inside working dir: $WORKING_DIR"

echo "listing files:"
ls -al

echo "fetch ethereum tests."
git clone https://github.com/ethereum/tests

echo "run evm2wasm tests"
${TESTETH} -t GeneralStateTests/stExample -- --testpath ${WORKING_DIR}/tests --singlenet "Byzantium" --singletest "add11" --vm athena --evmc evm1mode=evm2wasm.cpp
${TESTETH} -t GeneralStateTests/stStackTests -- --testpath ${WORKING_DIR}/tests --singlenet "Byzantium" --vm athena --evmc evm1mode=evm2wasm.cpp
