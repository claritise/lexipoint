"""publish_release.py: a release built and published from this Mac, in place of a CI workflow (firmware-base.md §6).
The whole flow runs against a fake runner: no git, gh or pio."""

import io
import json
import os
import subprocess
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import publish_release  # noqa: E402
import release_tag  # noqa: E402

SHA = "30148173a9ae8d7b1c2e3f4a5b6c7d8e9f001122"
REPO = "claritise/lexipoint"
TAG = release_tag.expected_tag(*release_tag.configured(), False)
RC_TAG = release_tag.expected_tag(*release_tag.configured(), True)


class FakeRunner(publish_release.Runner):
    """Answers each command the way a clean, pushed checkout with no release yet would; a test changes one answer."""

    def __init__(self, **changes):
        self.state = dict(porcelain="", head=SHA, on_main=True, published="", remote_tag="", release_exists=False,
                          origin=f"git@github.com:{REPO}.git", latest=[(TAG, [publish_release.asset_name(TAG)])],
                          head_after_build=SHA, porcelain_after_build="", create_fails=None, local_ini=False,
                          keys_clean=True)
        self.state.update(changes)
        self.steps, self.slept, self.built = [], 0, False

    def run(self, cmd, extra_env=None):
        s = self.state
        if cmd[:2] == ["git", "rev-parse"]:
            return (s["head_after_build"] if self.built else s["head"]) + "\n"
        if cmd[:2] == ["git", "status"]:
            return s["porcelain_after_build"] if self.built else s["porcelain"]
        if cmd[:2] == ["git", "fetch"]:
            return ""
        if cmd[:2] == ["git", "ls-remote"]:
            return f"{s['remote_tag']}\trefs/tags/{TAG}\n" if s["remote_tag"] else ""
        if cmd[:3] == ["git", "remote", "get-url"]:
            return s["origin"] + "\n"
        if cmd[:3] == ["gh", "release", "list"]:
            return s["published"]
        if cmd[:2] == ["gh", "api"]:  # one read, one consistent snapshot, as GitHub returns it
            latest, assets = s["latest"][0] if len(s["latest"]) == 1 else s["latest"].pop(0)
            if latest is None:
                raise subprocess.CalledProcessError(1, cmd, stderr="HTTP 404: Not Found")
            if latest == "interrupt":
                raise KeyboardInterrupt()
            if latest == "offline":
                raise subprocess.CalledProcessError(1, cmd, stderr="error connecting to api.github.com")
            return json.dumps({"tag_name": latest, "assets": [{"name": a} for a in assets]})
        raise AssertionError(f"unexpected command {cmd}")

    def succeeds(self, cmd):
        if cmd[:2] == ["git", "merge-base"]:
            return self.state["on_main"]
        if cmd[:3] == ["gh", "release", "view"]:
            return self.state["release_exists"]
        if cmd[1:] == ["scripts/lexipoint/keyscan.py"]:
            return self.state["keys_clean"]
        raise AssertionError(f"unexpected command {cmd}")

    def step(self, cmd, extra_env):
        if cmd[:3] == ["gh", "release", "create"] and self.state["create_fails"]:
            raise self.state["create_fails"]
        self.steps.append((cmd, extra_env))
        if cmd[:2] == ["pio", "run"] and "clean" not in cmd:
            self.built = True

    def sleep(self, seconds):
        self.slept += 1

    def exists(self, path):
        return path == publish_release.LOCAL_INI and self.state["local_ini"]


def publish(runner, prerelease=False, dry_run=False, environ=None):
    lines = []
    code = publish_release.publish(runner, prerelease, dry_run, environ or {}, out=lines.append)
    return code, "\n".join(lines)


def created(runner):
    return [cmd for cmd, _ in runner.steps if cmd[:3] == ["gh", "release", "create"]]


class Flow(unittest.TestCase):
    def test_a_release_builds_clean_then_publishes_with_its_asset(self):
        r = FakeRunner()
        code, out = publish(r)
        self.assertEqual(code, 0, out)
        cmds = [cmd for cmd, _ in r.steps]
        self.assertEqual(cmds[0], ["pio", "run", "-e", "x4pro-gh_release", "-t", "clean"])
        self.assertEqual(cmds[1], ["pio", "run", "-e", "x4pro-gh_release", "-j1"])
        [create] = created(r)
        self.assertEqual(create[:4], ["gh", "release", "create", TAG])
        self.assertTrue(create[4].endswith(publish_release.asset_name(TAG)))  # the file goes up with the release
        self.assertEqual(create[create.index("--target") + 1], SHA)
        self.assertIn("--latest", create)
        self.assertIn(f"{TAG} published", out)

    def test_a_dry_run_builds_and_publishes_nothing(self):
        r = FakeRunner()
        code, out = publish(r, dry_run=True)
        self.assertEqual((code, r.steps), (0, []))
        self.assertIn("gh release create", out)

    def test_a_prerelease_builds_the_rc_env_with_its_hash_and_isnt_latest(self):
        r = FakeRunner()
        self.assertEqual(publish(r, prerelease=True)[0], 0)
        self.assertEqual(r.steps[1], (["pio", "run", "-e", "x4pro-gh_release_rc", "-j1"],
                                      {"CROSSPOINT_RC_HASH": SHA[:publish_release.RC_HASH_LENGTH]}))
        [create] = created(r)
        self.assertEqual(create[3], RC_TAG)
        self.assertIn("--prerelease", create)
        self.assertNotIn("--latest", create)

    def test_every_problem_stops_it_before_the_build(self):
        for change in (dict(porcelain=" M src/main.cpp\n"), dict(on_main=False), dict(release_exists=True),
                       dict(remote_tag="0123456789abcdef"), dict(origin="git@github.com:someone/else.git"),
                       dict(published=f"{TAG}\n9.9.9-lexi.1\n"), dict(local_ini=True), dict(keys_clean=False)):
            with self.subTest(change):
                r = FakeRunner(**change)
                code, out = publish(r)
                self.assertEqual((code, r.steps), (1, []), out)
        r = FakeRunner()
        self.assertEqual(publish(r, environ={"PLATFORMIO_BUILD_FLAGS": "-DLEXIPOINT_DEV_HARNESS=1"})[0], 1)
        self.assertEqual(r.steps, [])

    def test_a_checkout_that_moved_during_the_build_is_not_published(self):
        for change in (dict(head_after_build="f" * 40), dict(porcelain_after_build=" M src/main.cpp\n")):
            with self.subTest(change):
                r = FakeRunner(**change)
                code, out = publish(r)
                self.assertEqual(code, 1)
                self.assertEqual(created(r), [])
                self.assertIn("nothing published", out)

    def test_a_late_latest_is_retried_then_accepted(self):
        r = FakeRunner(latest=[(None, []), ("1.6.5-lexi.0", ["old.bin"]), (TAG, [publish_release.asset_name(TAG)])])
        self.assertEqual(publish(r)[0], 0)
        self.assertEqual(r.slept, 2)

    def test_the_latest_release_without_its_asset_fails(self):
        r = FakeRunner(latest=[(TAG, ["something-else.bin"])])
        code, out = publish(r)
        self.assertEqual(code, 1)
        self.assertIn(f"not {publish_release.asset_name(TAG)}", out)

    def test_a_failed_latest_read_is_not_a_missing_release(self):
        # A login or network failure says nothing about the release: not retried into "devices won't be offered",
        # and the message says it is published, so it isn't published again.
        r = FakeRunner(latest=[("offline", [])])
        code, out = publish(r)
        self.assertEqual((code, r.slept), (1, 0))
        self.assertIn(f"{TAG} is published", out)
        self.assertIn("don't publish it again", out)
        code, out = publish(FakeRunner(latest=[("interrupt", [])]))  # stopped while checking: the same advice
        self.assertEqual(code, 1)
        self.assertIn("(stopped)", out)

    def test_a_failed_or_stopped_create_says_how_to_clean_up(self):
        for failure in (subprocess.CalledProcessError(1, ["gh"]), KeyboardInterrupt()):
            with self.subTest(failure):
                r = FakeRunner(create_fails=failure)
                code, out = publish(r)
                self.assertEqual(code, 1)
                self.assertIn(f"gh release delete {TAG}", out)

    def test_a_tag_that_appeared_during_the_build_stops_it(self):
        r = FakeRunner()
        real = r.run

        def run(cmd, extra_env=None):
            if cmd[:2] == ["git", "ls-remote"] and r.built:
                return f"0123456789abcdef\trefs/tags/{TAG}\n"
            return real(cmd, extra_env)
        r.run = run
        code, out = publish(r)
        self.assertEqual((code, created(r)), (1, []))
        self.assertIn("nothing published", out)

    def test_a_release_devices_wont_see_fails_loudly(self):
        r = FakeRunner(latest=[("1.6.5-lexi.0", [])])
        code, out = publish(r)
        self.assertEqual(code, 1)
        self.assertEqual(r.slept, publish_release.LATEST_TRIES - 1)
        self.assertIn("devices won't be offered", out)


class Main(unittest.TestCase):
    def main(self, argv, which=True, publish=None):
        err = io.StringIO()
        with mock.patch.object(publish_release.shutil, "which", return_value="/bin/x" if which else None), \
                mock.patch.object(publish_release, "publish", publish or mock.Mock(return_value=0)) as p, \
                mock.patch.object(sys, "stderr", err):
            code = publish_release.main(argv)
        return code, err.getvalue(), p

    def test_the_flags_reach_the_flow(self):
        code, _, p = self.main(["--prerelease", "--dry-run"])
        self.assertEqual(code, 0)
        _, prerelease, dry_run, _ = p.call_args.args
        self.assertEqual((prerelease, dry_run), (True, True))

    def test_a_missing_tool_stops_it(self):
        code, err, p = self.main([], which=False)
        self.assertEqual(code, 2)
        self.assertIn("isn't installed", err)
        p.assert_not_called()

    def test_a_failed_command_is_named_with_its_error(self):
        fail = mock.Mock(side_effect=subprocess.CalledProcessError(128, ["git", "fetch"], stderr="no network"))
        code, err, _ = self.main([], publish=fail)
        self.assertEqual(code, 1)
        self.assertIn("`git fetch` failed (128): no network", err)


class Checks(unittest.TestCase):
    def test_it_publishes_where_devices_look(self):
        # The updater's feed (LexiriseConfig.h) names the repo; published anywhere else, devices never see it.
        self.assertEqual(publish_release.releases_repo(), REPO)

    def test_remote_urls(self):
        for url in (f"git@github.com:{REPO}.git", f"https://github.com/{REPO}.git", f"https://github.com/{REPO}"):
            self.assertEqual(publish_release.repo_of_url(url), REPO, url)
        self.assertEqual(publish_release.repo_of_url("/Users/x/Projects/lexipoint"), "")

    def test_the_asset_is_what_the_updater_looks_for(self):
        self.assertEqual(publish_release.asset_name("1.6.5-lexi.2"), "lexipoint-1.6.5-lexi.2-x4pro.bin")
        with open(publish_release.CONFIG_H, encoding="utf-8") as f:
            self.assertIn(f'kReleaseAssetPrefix = "{release_tag.ASSET_PREFIX}";', f.read())
        longest = "9" * release_tag.MAX_TAG_LENGTH  # the longest tag still fits the updater's asset-name buffer
        self.assertEqual(len(publish_release.asset_name(longest)) + 1, release_tag.ASSET_NAME_BYTES)

    def test_both_envs_exist(self):
        with open(os.path.join(publish_release.FIRMWARE, "platformio.ini"), encoding="utf-8") as f:
            ini = f.read()
        for env in publish_release.ENVS.values():
            self.assertIn(f"[env:{env}]", ini)


if __name__ == "__main__":
    unittest.main()
