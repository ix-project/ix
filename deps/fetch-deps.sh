#!/bin/bash
# fetch-deps
#
# by default, fetch dependencies in DEPS depfile in the current targetdir
# requires git
set -e

usage ()
{
    echo "usage: $0 [-h] [-t targetdir] [depfile]"
}


init()
{
    #optional args
    argv__TARGETDIR=""

    getopt_results=$(getopt -s bash -o ht: --long help,targetdir: -- "$@")
    if test $? != 0
    then
        echo "$0: unrecognized option"
        exit 1
    fi
    eval set -- "$getopt_results"

    while true
    do
        case "$1" in
            -h|--help)
                usage
                exit 0
                ;;
            -t|--targetdir)
                argv__TARGETDIR="$2"
                shift 2
                ;;
            --)
                shift
                break
                ;;
            *)
                EXCEPTION=$Main__ParameterException
                EXCEPTION_MSG="unparseable option $1"
                exit "$EXCEPTION"
                ;;
        esac
    done
    if [ -n "$argv__TARGETDIR" ]; then
        if ! [ -d "$argv__TARGETDIR" ]; then
            echo "error"
            EXCEPTION=$Main__ParameterException
            EXCEPTION_MSG="invalid targetdir parameter $1"
            exit "$EXCEPTION"
        fi
    fi

    # positional args
    argv__DEPFILE=""
    if [ -n "$1" ]; then
        if [ ! -f "$1" ]; then
            echo "error"
            EXCEPTION=$Main__ParameterException
            EXCEPTION_MSG="invalid depfile parameter $1"
            exit "$EXCEPTION"
        fi
        argv__DEPFILE="$1"
    else
        argv__DEPFILE="$(dirname "$0")/DEPS"
    fi
}

init_exceptions()
{
    EXCEPTION=0
    EXCEPTION_MSG=""
    #Main__Unknown=1
    Main__ParameterException=2
}

cleanup()
{
    #clean dir structure in case of script failure
    echo "$0: cleanup..."
}

Main__interruptHandler()
{
    # @description signal handler for SIGINT
    echo "$0: SIGINT caught"
    exit
}
Main__terminationHandler()
{
    # @description signal handler for SIGTERM
    echo "$0: SIGTERM caught"
    exit
}
Main__exitHandler()
{
    # @description signal handler for end of the program (clean or unclean).
    # probably redundant call, we already call the cleanup in main.
    if [ "$EXCEPTION" -ne 0 ] ; then
        cleanup
        echo "$0: error : ${EXCEPTION_MSG}"
    fi
    exit
}

trap Main__interruptHandler INT
trap Main__terminationHandler TERM
trap Main__exitHandler EXIT

git_clone_checkout()
{
  git clone "$1" "$2" || echo "$0: warning: git clone failed."
  git -C "$2" checkout "$3" || echo "$0: warning git chechout failed."
}

Main__main()
{
    # init scipt temporals
    init_exceptions
    init "$@"
    #body
    if [[ -n "$argv__TARGETDIR" ]]; then
        DIR="$argv__TARGETDIR"
    else
        DIR="$(dirname "$0")"
    fi

    while IFS='' read -r line || [[ -n "$line" ]]; do
        if [[ $line == \#* || -z $line ]] ; then
            continue
        fi
        local a=( $line )
        local src="${a[0]}"
        local dst="${a[1]}"
        local tag="${a[2]}"
        git_clone_checkout "$src" "$DIR/$dst" "$tag"
    done < "$argv__DEPFILE"

    exit 0
}

# catch signals and exit
#trap exit INT TERM EXIT

Main__main "$@"

