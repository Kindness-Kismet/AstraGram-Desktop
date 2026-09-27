import unittest

from build_support.changelog import validate_changelog


VALID = "- Added a feature, so you can now use it\n\n---\n\n- 新增了一个功能，现在可以使用它\n"


class ChangelogTests(unittest.TestCase):
    def test_valid_notes_are_normalized(self):
        self.assertEqual(validate_changelog(VALID), VALID)
        self.assertEqual(validate_changelog("\n" + VALID.replace("\n", "\r\n")), VALID)

    def test_malformed_notes_fail(self):
        for broken in (
            "## 7.2.9.10\n\n" + VALID,
            "- Added a feature\n\n- 新增了一个功能\n",
            "- Added a feature\n---\n- 新增了一个功能\n",
            VALID + "\n---\n\n- Extra\n",
            "- Added a feature.\n\n---\n\n- 新增了一个功能\n",
            "- Added a feature\n\n---\n\n- 新增了一个功能。\n",
            "- Added a feature\n- Fixed a bug\n\n---\n\n- 新增了一个功能\n",
            "- Added a feature\n\n- Fixed a bug\n\n---\n\n- 新增了一个功能\n\n- 修复了一个错误\n",
        ):
            with self.subTest(broken=broken):
                with self.assertRaises(SystemExit):
                    validate_changelog(broken)


if __name__ == "__main__":
    unittest.main()
