#!/bin/bash

cat quack.data | nc -q 0 -u -N 127.0.0.1 5103
