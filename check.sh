#! /usr/bin/env bash

for i in {000..099}; do
    if [[ -n $(diff client_dir/file${i} server_dir/Alice/file${i}) ]]; then
        echo $(diff client_dir/file${i} server_dir/Alice/file${i})
        exit 1
    else
        echo "pass test${i}"
    fi
done

echo "PASS!!"