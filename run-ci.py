import argparse
from pathlib import Path
import sys


import tankerci
import tankerci.conan
import tankerci.cpp
import tankerci.git
from tankerci.conan import Profile


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
    build_and_test_parser.add_argument("--coverage", action="store_true")
    build_and_test_parser.add_argument("--remote", default="artifactory")

    build_and_test_parser.add_argument(
        "--profile",
        dest="profiles",
        nargs="+",
        type=str,
        required=True,
    )

    args = parser.parse_args()
    user_home = None
    if args.home_isolation:
        user_home = Path.cwd() / ".cache" / "conan" / args.remote

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

        with tankerci.conan.ConanContextManager(
            [args.remote, "conancenter"], conan_home=user_home
        ):
            built_path = tankerci.cpp.build(
                    host_profile=Profile(args.profiles),
                    build_profile=tankerci.conan.get_build_profile(),
                    coverage=args.coverage,
                    make_package=True,
                    )
            tankerci.cpp.check(built_path, coverage=args.coverage, ctest_flags=ctest_flags)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
