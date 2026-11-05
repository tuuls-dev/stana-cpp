#ifndef STANA_GPU_SHADER_PARSED_HPP
#define STANA_GPU_SHADER_PARSED_HPP
#include <src/tint/lang/wgsl/reader/reader.h>
#include <string>

namespace stana::gpu::detail::shader {
/// @brief Compiler-only bridge; source storage is destroyed after the AST that borrows it.
/// Allocated in its final shared owner before parsing, never copied or moved.
struct ParsedShader {
	explicit ParsedShader(const std::string &source)
		: file("application.wgsl", source),
		  program(tint::wgsl::reader::Parse(&file)) {}

	ParsedShader(const ParsedShader &) = delete;
	ParsedShader(ParsedShader &&) = delete;
	auto operator=(const ParsedShader &) -> ParsedShader & = delete;
	auto operator=(ParsedShader &&) -> ParsedShader & = delete;
	~ParsedShader() = default;

	tint::Source::File file;
	tint::Program program;
};
} // namespace stana::gpu::detail::shader
#endif
