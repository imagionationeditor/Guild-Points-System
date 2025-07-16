#!/bin/bash

modulename="mod-guild-points"

# Get sources
CURRENT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

source $CURRENT_PATH"/../conf/config_module.sh"

echo "Loading module $modulename"
