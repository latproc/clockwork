#!/bin/bash

usage() {
    echo "usage: $0 xml-file module-name"
}

if [[ "$#" < 2 ]]; then
    usage
    exit 1
fi

echo "available devices"
xmllint --xpath "/EtherCATInfo/Descriptions/Devices/Device[string(Type)='$2']/Type" "$1"

if [ ! -z "$3" ]; then
    echo
    echo "selected device: "
    xmllint --xpath "/EtherCATInfo/Descriptions/Devices/Device[string(Type)='$2']/Type[@RevisionNo='#x$3']" "$1"
    echo
    echo "Predefined Sync Managers: "
    xmllint --xpath "/EtherCATInfo/Descriptions/Devices/Device[string(Type)='$2']/Type[@RevisionNo='#x$3']/../Info/VendorSpecific/TwinCAT/AlternativeSmMapping/Name" "$1"
fi
