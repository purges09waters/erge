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
/**
 * Yul interpreter.
 */

#pragma once

#include <libyul/AsmDataForward.h>
#include <libyul/optimiser/ASTWalker.h>

#include <libsolutil/FixedHash.h>
#include <libsolutil/CommonData.h>

#include <libsolutil/Exceptions.h>

#include <map>

namespace solidity::yul
{
struct Dialect;
}

namespace solidity::yul::test
{
class InterpreterTerminatedGeneric: public util::Exception
{};

class ExplicitlyTerminated: public InterpreterTerminatedGeneric
{};

class StepLimitReached: public InterpreterTerminatedGeneric
{};

class TraceLimitReached: public InterpreterTerminatedGeneric
{};

enum class ControlFlowState
{
	Default,
	Continue,
	Break,
	Leave
};

struct InterpreterState
{
	bytes calldata;
	bytes returndata;
	std::map<u256, uint8_t> memory;
	/// This is different than memory.size() because we ignore gas.
	u256 msize;
	std::map<util::h256, util::h256> storage;
	u160 address = 0x11111111;
	u256 balance = 0x22222222;
	u256 selfbalance = 0x22223333;
	u160 origin = 0x33333333;
	u160 caller = 0x44444444;
	u256 callvalue = 0x55555555;
	/// Deployed code
	bytes code = util::asBytes("codecodecodecodecode");
	u256 gasprice = 0x66666666;
	u160 coinbase = 0x77777777;
	u256 timestamp = 0x88888888;
	u256 blockNumber = 1024;
	u256 difficulty = 0x9999999;
	u256 gaslimit = 4000000;
	u256 chainid = 0x01;
	/// Log of changes / effects. Sholud be structured data in the future.
	std::vector<std::string> trace;
	/// This is actually an input parameter that more or less limits the runtime.
	size_t maxTraceSize = 0;
	size_t maxSteps = 0;
	size_t numSteps = 0;
	ControlFlowState controlFlowState = ControlFlowState::Default;

	void dumpTraceAndState(std::ostream& _out) const;
};

/**
 * Scope structure built and maintained during execution.
 */
struct Scope
{
	/// Used for variables and functions. Value is nullptr for variables.
	std::map<YulString, FunctionDefinition const*> names;
	std::map<Block const*, std::unique_ptr<Scope>> subScopes;
	Scope* parent = nullptr;
};

/**
 * Yul interpreter.
 */
class Interpreter: public ASTWalker
{
public:
	static void run(InterpreterState& _state, Dialect const& _dialect, Block const& _ast);

	Interpreter(
		InterpreterState& _state,
		Dialect const& _dialect,
		Scope& _scope,
		std::map<YulString, u256> _variables = {}
	):
		m_dialect(_dialect), m_state(_state), m_variables(std::move(_variables)), m_scope(&_scope)
	{}

	void operator()(ExpressionStatement const& _statement) override;
	void operator()(Assignment const& _assignment) override;
	void operator()(VariableDeclaration const& _varDecl) override;
	void operator()(If const& _if) override;
	void operator()(Switch const& _switch) override;
	void operator()(FunctionDefinition const&) override;
	void operator()(ForLoop const&) override;
	void operator()(Break const&) override;
	void operator()(Continue const&) override;
	void operator()(Leave const&) override;
	void operator()(Block const& _block) override;

	std::vector<std::string> const& trace() const { return m_state.trace; }

	u256 valueOfVariable(YulString _name) const { return m_variables.at(_name); }

private:
	/// Asserts that the expression evaluates to exactly one value and returns it.
	u256 evaluate(Expression const& _expression);
	/// Evaluates the expression and returns its value.
	std::vector<u256> evaluateMulti(Expression const& _expression);

	void enterScope(Block const& _block);
	void leaveScope();

	/// Increment interpreter step count, throwing exception if step limit
	/// is reached.
	void incrementStep();

	Dialect const& m_dialect;
	InterpreterState& m_state;
	/// Values of variables.
	std::map<YulString, u256> m_variables;
	Scope* m_scope;
};

/**
 * Yul expression evaluator.
 */
class ExpressionEvaluator: public ASTWalker
{
public:
	ExpressionEvaluator(
		InterpreterState& _state,
		Dialect const& _dialect,
		Scope& _scope,
		std::map<YulString, u256> const& _variables
	):
		m_state(_state), m_dialect(_dialect), m_variables(_variables), m_scope(_scope)
	{}

	void operator()(Literal const&) override;
	void operator()(Identifier const&) override;
	void operator()(FunctionCall const& _funCall) override;

	/// Asserts that the expression has exactly one value and returns it.
	u256 value() const;
	/// Returns the list of values of the expression.
	std::vector<u256> values() const { return m_values; }

private:
	void setValue(u256 _value);

	/// Evaluates the given expression from right to left and
	/// stores it in m_value.
	void evaluateArgs(std::vector<Expression> const& _expr);

	InterpreterState& m_state;
	Dialect const& m_dialect;
	/// Values of variables.
	std::map<YulString, u256> const& m_variables;
	Scope& m_scope;
	/// Current value of the expression
	std::vector<u256> m_values;
};

}
