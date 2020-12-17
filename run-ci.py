import argparse
import os
from pathlib import Path
import sys


import tankerci
import tankerci.cpp
import tankerci.conan
import tankerci.git


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--isolate-conan-user-home",
        action="store_true",
        dest="home_isolation",
        default=False,
    )
    subparsers = parser.add_subparsers(title="subcommands", dest="command")

    build_and_test_parser = subparsers.add_parser("build-and-test")
    build_and_test_parser.add_argument("--profile", required=True)
    build_and_test_parser.add_argument("--coverage", action="store_true")

    subparsers.add_parser("mirror")

    args = parser.parse_args()
    if args.home_isolation:
        tankerci.conan.set_home_isolation()

    tankerci.conan.update_config()

    if args.command == "build-and-test":
        src_path = Path.cwd()
        build_path = "."  # tankerci.cpp.Builder runs ctest from the build directory
        # fmt: off
        ctest_flags = [
            "--build-and-test",
            src_path,
            build_path,
            "--build-generator", "Ninja",
            "--output-on-failure",
            "--test-command", "bin/test_tconcurrent",
        ]
        # fmt: on
        if sys.platform == "darwin":
            # When a macOS runner runs the tests, the ones waiting for a specific time will wait longer than requested.
            # Thus the tests fail. Funny thing is that they pass when running them by hand, on the slave...
            ctest_flags.append("--test-case-exclude=*[waiting]*")
        built_path = tankerci.cpp.build(args.profile, coverage=args.coverage)
        tankerci.cpp.check(built_path, coverage=args.coverage, ctest_flags=ctest_flags)
    elif args.command == "mirror":
        tankerci.git.mirror(github_url="git@github.com:TankerHQ/tconcurrent")
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
