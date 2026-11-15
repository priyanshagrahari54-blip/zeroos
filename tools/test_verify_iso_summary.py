#!/usr/bin/env python3
"""Regression tests for ISO verifier summary classification."""

import unittest

from verify_iso import Report, summary_line


class VerifyIsoSummaryTests(unittest.TestCase):
    def test_optional_pycdlib_warning_does_not_claim_iso_will_not_boot(self):
        report = Report()
        report.warn(
            "pycdlib not installed; independent cross-check skipped "
            "(pip install pycdlib enables it)"
        )

        summary = summary_line(report)

        self.assertIn("required ISO 9660 and El Torito bootability checks passed", summary)
        self.assertNotIn("THIS IMAGE WILL NOT BOOT", summary)
        self.assertNotIn("bootability NOT certified", summary)

    def test_waived_bootability_failure_is_explicitly_not_certified(self):
        report = Report(allow_unbootable=True)
        report.fail("NO El Torito boot record", bootability=True)

        summary = summary_line(report)

        self.assertIn("1 bootability check(s) waived", summary)
        self.assertIn("bootability NOT certified", summary)
        self.assertNotIn("bootability checks passed", summary)

    def test_optional_warning_is_counted_separately_from_waiver(self):
        report = Report(allow_unbootable=True)
        report.fail("NO El Torito boot record", bootability=True)
        report.warn("optional parser unavailable")

        summary = summary_line(report)

        self.assertIn("1 bootability check(s) waived and 1 optional warning(s)", summary)
        self.assertIn("bootability NOT certified", summary)

    def test_real_failure_takes_precedence_over_warnings(self):
        report = Report()
        report.warn("optional parser unavailable")
        report.fail("required kernel file missing")

        self.assertEqual(summary_line(report), "verify_iso: FAIL (1 check(s) failed)")

    def test_clean_report_keeps_bootable_success_summary(self):
        report = Report()
        self.assertEqual(
            summary_line(report),
            "verify_iso: PASS - image is a structurally bootable ISO 9660 / El Torito image",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
