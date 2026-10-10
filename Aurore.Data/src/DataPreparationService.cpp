#include <Aurore/Data/DataPreparationService.hpp>

#include "JavaRuntime.hpp"
#include "MinecraftDataGenerator.hpp"

#include <Aurore/Data/MinecraftSource.hpp>

#include <string>
#include <utility>

namespace {
	using Aurore::Data::MinecraftSourceError;
	using Aurore::Data::MinecraftSourceErrorCode;
	using Aurore::Data::RawDataPreparationErrorCode;
	using Aurore::Data::RawDataPreparationFailure;
	using Aurore::Data::Detail::JavaRuntimeError;
	using Aurore::Data::Detail::JavaRuntimeErrorCode;
	using Aurore::Data::Detail::MinecraftDataGeneratorError;
	using Aurore::Data::Detail::MinecraftDataGeneratorErrorCode;

	[[nodiscard]] std::string DescribeSourceError(MinecraftSourceErrorCode code) {
		switch (code) {
		case MinecraftSourceErrorCode::SourceNotImplemented: return "the requested Minecraft source mode is not implemented";
		case MinecraftSourceErrorCode::PathResolutionFailed: return "the Minecraft source path could not be resolved";
		case MinecraftSourceErrorCode::PathInspectionFailed: return "the Minecraft source path could not be inspected";
		case MinecraftSourceErrorCode::FileNotFound: return "the Minecraft source file does not exist";
		case MinecraftSourceErrorCode::NotRegularFile: return "the Minecraft source path is not a regular file";
		case MinecraftSourceErrorCode::FileOpenFailed: return "the Minecraft source file could not be opened";
		case MinecraftSourceErrorCode::FileReadFailed: return "the Minecraft source file could not be read";
		case MinecraftSourceErrorCode::EmptyFile: return "the Minecraft source file is empty";
		}
		return "Minecraft source resolution failed";
	}

	[[nodiscard]] std::string DescribeJavaError(JavaRuntimeErrorCode code) {
		switch (code) {
		case JavaRuntimeErrorCode::NotFound: return "a usable Java runtime could not be found";
		case JavaRuntimeErrorCode::PathResolutionFailed: return "the Java executable path could not be resolved";
		case JavaRuntimeErrorCode::PathInspectionFailed: return "the Java executable path could not be inspected";
		case JavaRuntimeErrorCode::NotRegularFile: return "the Java executable path is not a regular file";
		case JavaRuntimeErrorCode::ProcessFailed: return "the Java runtime could not be executed";
		case JavaRuntimeErrorCode::NonZeroExitCode: return "the Java runtime returned a non-zero status while being validated";
		case JavaRuntimeErrorCode::UnrecognizedVersion: return "the Java runtime version output could not be recognized";
		}
		return "Java runtime resolution failed";
	}

	[[nodiscard]] std::string DescribeGeneratorError(MinecraftDataGeneratorErrorCode code) {
		switch (code) {
		case MinecraftDataGeneratorErrorCode::InvalidSourceArtifact: return "the resolved Minecraft source artifact is invalid";
		case MinecraftDataGeneratorErrorCode::InvalidJavaRuntime: return "the resolved Java runtime is invalid";
		case MinecraftDataGeneratorErrorCode::IncompatibleJavaVersion: return "the Java runtime is too old for the target Minecraft generator";
		case MinecraftDataGeneratorErrorCode::StagingPathResolutionFailed: return "the raw-data staging path could not be resolved";
		case MinecraftDataGeneratorErrorCode::StagingCleanupFailed: return "stale raw-data staging output could not be removed";
		case MinecraftDataGeneratorErrorCode::StagingDirectoryCreationFailed: return "the raw-data staging directory could not be created";
		case MinecraftDataGeneratorErrorCode::ProcessFailed: return "the Minecraft data-generator process could not be executed";
		case MinecraftDataGeneratorErrorCode::GeneratorFailed: return "the Minecraft data generator returned a non-zero status";
		case MinecraftDataGeneratorErrorCode::OutputInspectionFailed: return "the generated Minecraft output could not be inspected";
		case MinecraftDataGeneratorErrorCode::RegistryReportMissing: return "the generated Minecraft registry report is missing";
		case MinecraftDataGeneratorErrorCode::ServerDataMissing: return "the generated Minecraft server data is missing";
		}
		return "Minecraft raw-data generation failed";
	}

	[[nodiscard]] RawDataPreparationFailure MakeSourceFailure(const MinecraftSourceError& error) {
		return RawDataPreparationFailure{
			.Code = RawDataPreparationErrorCode::SourceResolutionFailed,
			.Message = DescribeSourceError(error.Code),
			.Path = error.Path,
			.SystemError = error.SystemError,
		};
	}

	[[nodiscard]]
	RawDataPreparationFailure MakeJavaFailure(const JavaRuntimeError& error) {
		RawDataPreparationFailure result{
			.Code = RawDataPreparationErrorCode::JavaResolutionFailed,
			.Message = DescribeJavaError(error.Code),
			.Path = error.Path,
			.ExitCode = error.ExitCode,
			.StandardError = error.Output,
		};

		if (error.ProcessFailure)
			result.SystemError = error.ProcessFailure->SystemError;

		return result;
	}

	[[nodiscard]]
	RawDataPreparationFailure MakeGenerationFailure(const MinecraftDataGeneratorError& error) {
		RawDataPreparationFailure result{
			.Code = RawDataPreparationErrorCode::DataGenerationFailed,
			.Message = DescribeGeneratorError(error.Code),
			.Path = error.Path,
			.SystemError = error.SystemError,
			.ExitCode = error.ExitCode,
			.StandardOutput = error.StdOut,
			.StandardError = error.StdErr,
		};

		if (!result.SystemError && error.ProcessFailure)
			result.SystemError = error.ProcessFailure->SystemError;

		return result;
	}
}

namespace Aurore::Data {
	std::expected<RawDataPreparationResult, RawDataPreparationFailure> DataPreparationService::PrepareRaw(const DataPreparationRequest& request, const DataPreparationRuntimeOptions& runtime) {
		auto source = MinecraftSourceResolver::Resolve(request);
		if (!source) return std::unexpected(MakeSourceFailure(source.error()));
		auto java = Detail::JavaRuntimeResolver::Resolve(Detail::JavaRuntimeRequest{ .ExplicitExecutable = runtime.JavaExecutable });
		if (!java) return std::unexpected(MakeJavaFailure(java.error()));
		auto generated = Detail::MinecraftDataGenerator::Generate(*source, *java, request.GetCacheRoot() / ".staging");
		if (!generated) return std::unexpected(MakeGenerationFailure(generated.error()));
		return RawDataPreparationResult{
			.Version = generated->Version,

			.SourceJarPath = source->JarPath,
			.SourceFileSize = source->FileSize,
			.SourceSha1 = source->Sha1,

			.JavaExecutable = java->Executable,
			.JavaMajorVersion = java->MajorVersion,
			.JavaVersionText = java->VersionText,

			.GenerationRoot = generated->Root,
			.ReportsRoot = generated->ReportsRoot,
			.DataRoot = generated->DataRoot,
		};
	}
}
