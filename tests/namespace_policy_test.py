"""Exercise namespace conventions against equivalent C++ namespace spellings."""

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("namespace_policy", Path(__file__).resolve().parents[1] / "tools/namespace_policy.py")
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class NamespacePolicyTests(unittest.TestCase):
    def test_functional_namespaces(self):
        self.assertEqual(policy.violations("""
            export module stana:rendering;
            export namespace stana::rendering { struct Drawing {}; }
            namespace stana { namespace rendering { namespace detail { struct Cache {}; } } }
            namespace stana::testing::fixtures::platform::macos { struct Fixture {}; }
        """), [])

    def test_root_helpers(self):
        self.assertEqual(policy.violations("""
            namespace stana { namespace detail {
                [[nodiscard]] inline auto nextIdentity() -> int { return 1; }
                inline void requireFinite(double value) { static_cast<void>(value); }
                inline void requireNonnegative(double value) { requireFinite(value); }
            } }
        """), [])

    def test_retired_namespace_spellings(self):
        for source in (
            "namespace stana::native { struct Window {}; }",
            "namespace stana { namespace native { struct Window {}; } }",
            "namespace stana { namespace fixtures {} }",
            "namespace stana { namespace native = platform::detail; }",
            "namespace stana::graphics_detail {}",
            "stana::native::run();",
        ):
            with self.subTest(source=source):
                self.assertTrue(policy.violations(source))

    def test_root_detail_not_a_dumping_ground(self):
        for declaration in ("struct Cache {};", "using Cache = int;", "inline int cache = 0;", "void flush();", "void flush(decltype(nextIdentity()) value);", "void flush() noexcept(noexcept(nextIdentity()));", "auto cache = nextIdentity();", "namespace rendering {}"):
            with self.subTest(declaration=declaration):
                self.assertTrue(policy.violations(f"namespace stana {{ namespace detail {{ {declaration} }} }}"))

    def test_root_detail_alias(self):
        self.assertTrue(policy.violations("namespace stana { namespace detail = rendering::detail; }"))

    def test_retired_modules(self):
        for source in ("export module stana.graphics;", "import stana.text;", "export import stana.reactive;", "export module separate;"):
            with self.subTest(source=source):
                self.assertTrue(policy.violations(source))

    def test_comments_and_shader_strings_are_not_cpp(self):
        self.assertEqual(policy.violations('''
            // namespace stana::native {}
            /* import stana.graphics; */
            constexpr auto shader = R"wgsl(namespace stana::shader_detail {})wgsl";
            constexpr auto message = "namespace stana::native {}";
        '''), [])


if __name__ == "__main__":
    unittest.main()
