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
            "(type:Output) EL2838_24, 0x7000, 1;",
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
        self.assertIn("EL2838_24, 0x7000, 1, 0x1601;", output)

    def test_missing_legacy_position_prevents_conversion(self):
        text = "O_Test POINT EL2838_24, 3;\n"
        output, count, errors = converter.convert_text(
            text, pathlib.Path("test.lpc"), {"EL2838_24": 24}, {24: []})
        self.assertEqual(text, output)
        self.assertEqual(0, count)
        self.assertEqual(1, len(errors))

    def test_preserves_analogue_settings_after_object_selector(self):
        text = "I_Test ANALOGINPUT (type:Input) EL3124_18, 10, Settings;\n"
        output, count, errors = converter.convert_text(
            text, pathlib.Path("test.lpc"), {"EL3124_18": 18}, {18: [
                {"position": 10, "index": 0x6000, "subindex": 17,
                 "pdo_index": 0x1A00}
            ]})
        self.assertEqual([], errors)
        self.assertEqual(1, count)
        self.assertIn("EL3124_18, 0x6000, 17, Settings;", output)

    def test_existing_positional_selector_is_not_rewritten(self):
        text = "O_Test POINT EL2838_24, 0x7000, 1;\n"
        output, count, errors = converter.convert_text(
            text, pathlib.Path("test.lpc"), {}, {})
        self.assertEqual(text, output)
        self.assertEqual(0, count)
        self.assertEqual([], errors)

    def test_load_topology_prefers_configured_entry_positions(self):
        topology_file = pathlib.Path(self.id().replace(".", "_") + ".json")
        try:
            topology_file.write_text("""[{
              "position": 4,
              "sync_managers": [{"pdos": [{"index": 5632, "entries": [
                {"pos": 2, "index": 0, "subindex": 0, "bit_length": 5}
              ]}]}],
              "configured_sync_managers": [{"pdos": [{"index": 5632, "entries": [
                {"pos": 2, "index": 28672, "subindex": 3, "bit_length": 1}
              ]}]}]
            }]""")
            topology = converter.load_topology(topology_file, {4})
        finally:
            topology_file.unlink(missing_ok=True)
        self.assertEqual(0x7000, topology[4][0]["index"])
        self.assertEqual(3, topology[4][0]["subindex"])

    def test_config_file_module_requires_configured_topology(self):
        topology_file = pathlib.Path(self.id().replace(".", "_") + ".json")
        try:
            topology_file.write_text("""[{
              "position": 4,
              "sync_managers": []
            }]""")
            with self.assertRaisesRegex(ValueError, "configured_sync_managers"):
                converter.load_topology(topology_file, {4})
        finally:
            topology_file.unlink(missing_ok=True)

    def test_rejects_alignment_entry(self):
        text = "O_Test POINT EL5152_04, 2;\n"
        output, count, errors = converter.convert_text(
            text, pathlib.Path("test.lpc"), {"EL5152_04": 4}, {4: [
                {"position": 2, "index": 0, "subindex": 0,
                 "pdo_index": 0x1600, "bit_length": 5}
            ]})
        self.assertEqual(text, output)
        self.assertEqual(0, count)
        self.assertIn("alignment entry", errors[0])


if __name__ == "__main__":
    unittest.main()
