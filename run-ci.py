import argparse
import sys

from path import Path
import ci
import ci.cpp
import ci.git


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(title="subcommands", dest="command")

    build_and_test_parser = subparsers.add_parser("build-and-test")
    build_and_test_parser.add_argument(
        "--profile", dest="profile", required=True
    )

    deploy_parser = subparsers.add_parser("mirror")

    args = parser.parse_args()

    if args.command == "build-and-test":
        build_path = Path.getcwd() / "build" / args.profile
        build_path.makedirs_p()
        src_path = Path.getcwd()
        ci.cpp.update_conan_config(sys.platform.lower())
        # fmt: off
        ci.cpp.run_conan(
            "install", src_path,
            "--profile", args.profile,
            "--install-folder", build_path
        )
        # fmt: on
        ci.run("cmake", src_path, "-G", "Ninja", cwd=build_path)
        # fmt: off
        ctest_flags = [
            "--build-and-test",
            src_path, build_path,
            "--build-generator", "Ninja",
            "--output-on-failure",
            "--test-command", "bin/test_tconcurrent"
        ]
        # fmt: on
        if args.profile == "macos":
            # When a macOS runner runs the tests, the ones waiting for a specific time will wait longer than requested.
            # Thus the tests fail. Funny thing is that they pass when running them by hand, on the slave...
            ctest_flags.append("--test-case-exclude=*[waiting]*")
        ci.run("ctest", *ctest_flags, cwd=build_path)
    elif args.command == "mirror":
        ci.git.mirror(github_url="git@github.com:TankerHQ/tconcurrent")
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
