#!/bin/bash

printf $@ | nc -q 0 -u -N 127.0.0.1 5103
