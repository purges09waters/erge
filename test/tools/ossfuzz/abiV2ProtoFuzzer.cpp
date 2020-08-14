/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#include <test/EVMHost.h>
#include <test/tools/ossfuzz/abiV2FuzzerCommon.h>
#include <test/tools/ossfuzz/protoToAbiV2.h>

#include <evmone/evmone.h>
#include <src/libfuzzer/libfuzzer_macro.h>

#include <fstream>

static evmc::VM evmone = evmc::VM{evmc_create_evmone()};

using namespace solidity::test::abiv2fuzzer;
using namespace solidity::test;
using namespace solidity::util;
using namespace solidity;
using namespace std;

namespace
{
/// Test function returns a uint256 value
static size_t const expectedOutputLength = 32;
/// Expected output value is decimal 0
static uint8_t const expectedOutput[expectedOutputLength] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/// Compares the contents of the memory address pointed to
/// by `_result` of `_length` bytes to the expected output.
/// Returns true if `_result` matches expected output, false
/// otherwise.
bool isOutputExpected(uint8_t const* _result, size_t _length)
{
	if (_length != expectedOutputLength)
		return false;

	return (memcmp(_result, expectedOutput, expectedOutputLength) == 0);
}

/// Accepts a reference to a user-specified input and returns an
/// evmc_message with all of its fields zero initialized except
/// gas and input fields.
/// The gas field is set to the maximum permissible value so that we
/// don't run into out of gas errors. The input field is copied from
/// user input.
evmc_message initializeMessage(bytes const& _input)
{
	// Zero initialize all message fields
	evmc_message msg = {};
	// Gas available (value of type int64_t) is set to its maximum
	// value.
	msg.gas = std::numeric_limits<int64_t>::max();
	msg.input_data = _input.data();
	msg.input_size = _input.size();
	return msg;
}

/// Accepts host context implementation, and keccak256 hash of the function
/// to be called at a specified address in the simulated blockchain as
/// input and returns the result of the execution of the called function.
evmc::result executeContract(
	EVMHost& _hostContext,
	bytes const& _functionHash,
	evmc_address _deployedAddress
)
{
	evmc_message message = initializeMessage(_functionHash);
	message.destination = _deployedAddress;
	message.kind = EVMC_CALL;
	return _hostContext.call(message);
}

/// Accepts a reference to host context implementation and byte code
/// as input and deploys it on the simulated blockchain. Returns the
/// result of deployment.
evmc::result deployContract(EVMHost& _hostContext, bytes const& _code)
{
	evmc_message message = initializeMessage(_code);
	message.kind = EVMC_CREATE;
	return _hostContext.call(message);
}
}

DEFINE_PROTO_FUZZER(Contract const& _input)
{
	string contract_source = ProtoConverter{}.contractToString(_input);

	if (const char* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		// With libFuzzer binary run this to generate the solidity source file x.sol from a proto
		// input: PROTO_FUZZER_DUMP_PATH=x.sol ./a.out proto-input
		ofstream of(dump_path);
		of << contract_source;
	}

	// Raw runtime byte code generated by solidity
	bytes byteCode;
	std::string hexEncodedInput;

	try
	{
		// Compile contract generated by the proto fuzzer
		SolidityCompilationFramework solCompilationFramework;
		std::string contractName = ":C";
		byteCode = solCompilationFramework.compileContract(contract_source, contractName);
		Json::Value methodIdentifiers = solCompilationFramework.getMethodIdentifiers();
		// We always call the function test() that is defined in proto converter template
		hexEncodedInput = methodIdentifiers["test()"].asString();
	}
	// Ignore stack too deep errors during compilation
	catch (evmasm::StackTooDeepException const&)
	{
		return;
	}
	// Do not ignore other compilation failures
	catch (Exception const&)
	{
		throw;
	}

	if (const char* dump_path = getenv("PROTO_FUZZER_DUMP_CODE"))
	{
		ofstream of(dump_path);
		of << toHex(byteCode);
	}

	// We target the default EVM which is the latest
	langutil::EVMVersion version = {};
	EVMHost hostContext(version, evmone);

	// Deploy contract and signal failure if deploy failed
	evmc::result createResult = deployContract(hostContext, byteCode);
	solAssert(
		createResult.status_code == EVMC_SUCCESS,
		"Proto ABIv2 Fuzzer: Contract creation failed"
	);

	// Execute test function and signal failure if EVM reverted or
	// did not return expected output on successful execution.
	evmc::result callResult =
		executeContract(hostContext, fromHex(hexEncodedInput), createResult.create_address);

	// We don't care about EVM One failures other than EVMC_REVERT
	solAssert(callResult.status_code != EVMC_REVERT, "Proto ABIv2 fuzzer: EVM One reverted");
	if (callResult.status_code == EVMC_SUCCESS)
		solAssert(
			isOutputExpected(callResult.output_data, callResult.output_size),
			"Proto ABIv2 fuzzer: ABIv2 coding failure found"
		);
}
