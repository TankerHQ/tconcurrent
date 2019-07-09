import argparse
import os
import sys

from path import Path

import ci
import ci.android
import ci.cpp
import ci.conan
import ci.ios
import ci.git


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--isolate-conan-user-home", action="store_true", dest="home_isolation", default=False)
    subparsers = parser.add_subparsers(title="subcommands", dest="command")

    build_and_test_parser = subparsers.add_parser("build-and-test")
    build_and_test_parser.add_argument("--profile", required=True)
    build_and_test_parser.add_argument("--coverage", action="store_true")

    subparsers.add_parser("deploy")
    subparsers.add_parser("mirror")


    args = parser.parse_args()
    if args.home_isolation:
        ci.conan.set_home_isolation()

    ci.cpp.update_conan_config()

    if args.command == "build-and-test":
        src_path = Path.getcwd()
        build_path = "."  # ci.cpp.Builder runs ctest from the build directory
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
        built_path = ci.cpp.build(args.profile, coverage=args.coverage)
        ci.cpp.check(built_path, coverage=args.coverage, ctest_flags=ctest_flags)
    elif args.command == "deploy":
        git_tag = os.environ["CI_COMMIT_TAG"]
        version = ci.version_from_git_tag(git_tag)
        ci.bump_files(version)
        ci.cpp.build_recipe(
            Path.getcwd(),
            conan_reference=f"tconcurrent/{version}@tanker/stable",
            upload=True,
        )
    elif args.command == "mirror":
        ci.git.mirror(github_url="git@github.com:TankerHQ/tconcurrent")
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
