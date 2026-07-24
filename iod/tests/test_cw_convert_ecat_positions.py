import importlib.util
import pathlib
import unittest

TOOL = pathlib.Path(__file__).parents[1] / "tools" / "cw_convert_ecat_positions.py"
SPEC = importlib.util.spec_from_file_location("converter", TOOL)
converter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(converter)


class ConverterTests(unittest.TestCase):
    def test_adds_selector_and_preserves_legacy_position(self):
        text = "O_Test POINT (type:Output) EL2838_24, 0;\n"
        modules = {"EL2838_24": 24}
        topology = {24: [
            {"position": 0, "index": 0x7000, "subindex": 1, "pdo_index": 0x1600}
        ]}
        output, count, errors = converter.convert_text(
            text, pathlib.Path("test.lpc"), modules, topology)
        self.assertEqual([], errors)
        self.assertEqual(1, count)
        self.assertIn(
            "(type:Output, entry_index:0x7000, entry_subindex:1) EL2838_24, 0;",
            output,
        )

    def test_adds_pdo_discriminator_for_duplicate_object(self):
        text = "O_Test POINT EL2838_24, 1;\n"
        modules = {"EL2838_24": 24}
        topology = {24: [
            {"position": 0, "index": 0x7000, "subindex": 1, "pdo_index": 0x1600},
            {"position": 1, "index": 0x7000, "subindex": 1, "pdo_index": 0x1601},
        ]}
        output, _, errors = converter.convert_text(
            text, pathlib.Path("test.lpc"), modules, topology)
        self.assertEqual([], errors)
        self.assertIn("entry_pdo_index:0x1601", output)

    def test_missing_legacy_position_prevents_conversion(self):
        text = "O_Test POINT EL2838_24, 3;\n"
        output, count, errors = converter.convert_text(
            text, pathlib.Path("test.lpc"), {"EL2838_24": 24}, {24: []})
        self.assertEqual(text, output)
        self.assertEqual(0, count)
        self.assertEqual(1, len(errors))

    def test_existing_selector_is_not_rewritten(self):
        text = (
            "O_Test POINT (entry_index:0x7000, entry_subindex:1) "
            "EL2838_24, 0;\n"
        )
        output, count, errors = converter.convert_text(
            text, pathlib.Path("test.lpc"), {}, {})
        self.assertEqual(text, output)
        self.assertEqual(0, count)
        self.assertEqual([], errors)


if __name__ == "__main__":
    unittest.main()
