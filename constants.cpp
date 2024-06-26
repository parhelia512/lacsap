#include "constants.h"
#include "expr.h"
#include "token.h"

#include <cmath>
#include <iostream>
#include <sstream>

#include <llvm/Support/Casting.h>

namespace Constants
{

    template<>
    Token IntConstDecl::Translate() const
    {
	return Token(Token::Integer, loc, value);
    }
    template<>
    void IntConstDecl::dump() const
    {
	std::cerr << "IntConstDecl: " << Value() << std::endl;
    }

    Token EnumConstDecl::Translate() const
    {
	return Token(Token::Integer, loc, value);
    }

    void EnumConstDecl::dump() const
    {
	std::cerr << "EnumConstDecl: " << Value() << std::endl;
    }
    template<>
    Token RealConstDecl::Translate() const
    {
	return Token(loc, value);
    }
    template<>
    void RealConstDecl::dump() const
    {
	std::cerr << "RealConstDecl: " << Value() << std::endl;
    }
    template<>
    Token CharConstDecl::Translate() const
    {
	return Token(Token::Char, loc, static_cast<uint64_t>(value));
    }
    template<>
    void CharConstDecl::dump() const
    {
	std::cerr << "CharConstDecl: " << Value() << std::endl;
    }
    template<>
    Token BoolConstDecl::Translate() const
    {
	std::string s = (value) ? "true" : "false";
	return Token(Token::Identifier, loc, s);
    }

    template<>
    void BoolConstDecl::dump() const
    {
	std::cerr << "BoolConstDecl: " << Value() << std::endl;
    }

    Token StringConstDecl::Translate() const
    {
	return Token(Token::StringLiteral, loc, value);
    }

    void StringConstDecl::dump() const
    {
	std::cerr << "StringConstDecl: " << Value() << std::endl;
    }

    void CompoundConstDecl::dump() const
    {
	std::cerr << "CompoundConstDecl: ";
	expr->dump();
	std::cerr << std::endl;
    }

    void RangeConstDecl::dump() const
    {
	std::cerr << "RangeConstDecl";
	range.DoDump();
	std::cerr << std::endl;
    }

    void SetConstDecl::dump() const
    {
	std::cerr << "SetConstDecl [";
	for (auto v : set)
	{
	    v->dump();
	}
	std::cerr << "]" << std::endl;
    }

    static bool GetAsReal(double& lValue, double& rValue, const ConstDecl& lhs, const ConstDecl& rhs)
    {

	const auto lhsR = llvm::dyn_cast<RealConstDecl>(&lhs);
	const auto rhsR = llvm::dyn_cast<RealConstDecl>(&rhs);
	const auto lhsI = llvm::dyn_cast<IntConstDecl>(&lhs);
	const auto rhsI = llvm::dyn_cast<IntConstDecl>(&rhs);

	bool ok = rhsR && lhsR;
	if (lhsR)
	{
	    lValue = lhsR->Value();
	}
	if (rhsR)
	{
	    rValue = rhsR->Value();
	}
	if (lhsR || rhsR)
	{
	    if (rhsI)
	    {
		rValue = rhsI->Value();
		ok = true;
	    }
	    if (lhsI)
	    {
		lValue = lhsI->Value();
		ok = true;
	    }
	}
	return ok;
    }

    static bool GetAsString(std::string& lValue, std::string& rValue, const ConstDecl* lhs,
                            const ConstDecl* rhs)
    {
	const auto lhsS = llvm::dyn_cast<StringConstDecl>(lhs);
	const auto rhsS = llvm::dyn_cast<StringConstDecl>(rhs);
	const auto lhsC = llvm::dyn_cast<CharConstDecl>(lhs);
	const auto rhsC = llvm::dyn_cast<CharConstDecl>(rhs);
	bool       ok = lhsS && rhsS;
	if (lhsS)
	{
	    lValue = lhsS->Value();
	}
	if (rhsS)
	{
	    rValue = rhsS->Value();
	}
	if (lhsS || rhsS)
	{
	    if (lhsC)
	    {
		lValue = lhsC->Value();
		ok = true;
	    }
	    if (rhsC)
	    {
		rValue = rhsC->Value();
		ok = true;
	    }
	}
	return ok;
    }

    ConstDecl* ErrorConst(const std::string& msg)
    {
	std::cerr << "Error: " << msg << std::endl;
	return 0;
    }

    template<typename FN>
    ConstDecl* DoRealMath(const ConstDecl& lhs, const ConstDecl& rhs, FN fn)
    {
	double rValue;
	double lValue;
	if (!GetAsReal(lValue, rValue, lhs, rhs))
	{
	    return 0;
	}
	return new RealConstDecl(Location(), fn(lValue, rValue));
    }

    template<typename FN>
    ConstDecl* DoIntegerMath(const ConstDecl& lhs, const ConstDecl& rhs, FN fn)
    {
	const auto lhsI = llvm::dyn_cast<IntConstDecl>(&lhs);
	const auto rhsI = llvm::dyn_cast<IntConstDecl>(&rhs);
	if (lhsI && rhsI)
	{
	    return new IntConstDecl(Location(), fn(lhsI->Value(), rhsI->Value()));
	}
	return 0;
    }

    ConstDecl* operator+(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	ConstDecl* v = DoRealMath(rhs, lhs, [](double lv, double rv) { return lv + rv; });
	if (!v)
	{
	    v = DoIntegerMath(rhs, lhs, [](uint64_t lv, uint64_t rv) { return lv + rv; });
	}
	if (!v)
	{
	    std::string rValue;
	    std::string lValue;
	    if (GetAsString(lValue, rValue, &lhs, &rhs))
	    {
		return new StringConstDecl(Location(), lValue + rValue);
	    }
	    return ErrorConst("Invalid operand for +");
	}
	return v;
    }

    ConstDecl* operator-(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	ConstDecl* v = DoRealMath(lhs, rhs, [](double lv, double rv) { return lv - rv; });
	if (!v)
	{
	    v = DoIntegerMath(lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv - rv; });
	}
	if (!v)
	{
	    return ErrorConst("Invalid operand for -");
	}
	return v;
    }

    ConstDecl* operator*(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	ConstDecl* v = DoRealMath(lhs, rhs, [](double lv, double rv) { return lv * rv; });
	if (!v)
	{
	    v = DoIntegerMath(lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv * rv; });
	}
	if (!v)
	{
	    return ErrorConst("Invalid operand for *");
	}
	return v;
    }

    ConstDecl* operator/(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	ConstDecl* v = DoRealMath(lhs, rhs, [](double lv, double rv) { return lv / rv; });
	if (!v)
	{
	    v = DoIntegerMath(lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv / rv; });
	}
	if (!v)
	{
	    return ErrorConst("Invalid operand for /");
	}
	return v;
    }

    template<typename FN>
    static ConstDecl* IntegerOrErrorMath(const ConstDecl& lhs, const ConstDecl& rhs, FN func,
                                         const std::string& oper)
    {
	if (ConstDecl* v = DoIntegerMath(lhs, rhs, func))
	{
	    return v;
	}
	return ErrorConst("Invalid operand for " + oper);
    }

    ConstDecl* operator%(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	return IntegerOrErrorMath(
	    lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv % rv; }, "mod");
    }

    ConstDecl* operator&(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	return IntegerOrErrorMath(
	    lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv & rv; }, "and");
    }

    ConstDecl* operator|(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	return IntegerOrErrorMath(
	    lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv | rv; }, "or");
    }

    ConstDecl* operator^(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	return IntegerOrErrorMath(
	    lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv ^ rv; }, "xor");
    }

    ConstDecl* operator<<(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	return IntegerOrErrorMath(
	    lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv << rv; }, "shl");
    }

    ConstDecl* operator>>(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	return IntegerOrErrorMath(
	    lhs, rhs, [](uint64_t lv, uint64_t rv) { return lv >> rv; }, "shr");
    }

    template<typename T>
    static const ConstDecl* UpdateValueSameType(const ConstDecl* cd, T func)
    {
	if (auto intConst = llvm::dyn_cast<IntConstDecl>(cd))
	{
	    return new IntConstDecl(intConst->Loc(), func(intConst->Value()));
	}
	if (auto enumConst = llvm::dyn_cast<EnumConstDecl>(cd))
	{
	    return new EnumConstDecl(enumConst->Type(), enumConst->Loc(), func(enumConst->Value()));
	}
	return 0;
    }

    template<typename T>
    static T PowerToInt(T x, int y)
    {
	if (y == 0)
	{
	    return 1;
	}
	if (y < 0)
	{
	    return 1 / PowerToInt(x, -y);
	}
	ICE_IF(y <= 0, "Expect y > 0");
	T res = 1;
	do
	{
	    res *= x;
	    y--;
	} while (y > 0);
	return res;
    }

    ConstDecl* Pow(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	auto intConst = llvm::dyn_cast<IntConstDecl>(&rhs);
	if (!intConst)
	{
	    return 0;
	}
	int y = intConst->Value();
	if (auto ic = llvm::dyn_cast<IntConstDecl>(&lhs))
	{
	    int x = ic->Value();
	    return new IntConstDecl(ic->Loc(), PowerToInt(x, y));
	}

	if (auto rc = llvm::dyn_cast<RealConstDecl>(&lhs))
	{
	    double x = rc->Value();
	    return new RealConstDecl(rc->Loc(), PowerToInt(x, y));
	}
	return 0;
    }

    ConstDecl* Power(const ConstDecl& lhs, const ConstDecl& rhs)
    {
	const RealConstDecl* l = llvm::cast<RealConstDecl>(ToRealConstDecl(&lhs));
	const RealConstDecl* r = llvm::cast<RealConstDecl>(ToRealConstDecl(&rhs));
	double               x = l->Value();
	double               y = r->Value();
	return new RealConstDecl(l->Loc(), exp(log(x) * y));
    }

    using ConstArgs = std::vector<const ConstDecl*>;
    using Func = std::function<const ConstDecl*(const ConstArgs&)>;
    struct EvaluableFunc
    {
	const char* name;
	size_t      minArgs;
	size_t      maxArgs;
	Func        func;
    };

    static std::vector<EvaluableFunc> evaluableFunctions = {
	{ "chr", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto intConst = llvm::dyn_cast<IntConstDecl>(args[0]))
	      {
	          return new CharConstDecl(intConst->Loc(), (char)intConst->Value());
	      }
	      return 0;
	  } },
	{ "succ", 1, 2,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      int n = 1;
	      if (args.size() > 1)
	      {
	          if (auto intConst = llvm::dyn_cast<IntConstDecl>(args[1]))
	          {
		      n = intConst->Value();
	          }
	          else
	          {
		      return ErrorConst("Expected integer as second argument to 'succ'");
	          }
	      }
	      return UpdateValueSameType(args[0], [n](int64_t v) { return v + n; });
	  } },
	{ "pred", 1, 2,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      int n = 1;
	      if (args.size() > 1)
	      {
	          if (auto intConst = llvm::dyn_cast<IntConstDecl>(args[1]))
	          {
		      n = intConst->Value();
	          }
	          else
	          {
		      return ErrorConst("Expected integer as second argument to 'pred'");
	          }
	      }
	      return UpdateValueSameType(args[0], [n](int64_t v) { return v - n; });
	  } },
	{ "ord", 1, 1, [](const ConstArgs& args) -> const ConstDecl*
	  { return new IntConstDecl(args[0]->Loc(), ToInt(args[0])); } },
	{ "length", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto strConst = llvm::dyn_cast<StringConstDecl>(args[0]))
	      {
	          return new IntConstDecl(strConst->Loc(), strConst->Value().length());
	      }
	      return 0;
	  } },
	{ "sin", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<RealConstDecl>(args[0]))
	      {
	          return new RealConstDecl(rc->Loc(), sin(rc->Value()));
	      }
	      return 0;
	  } },
	{ "cos", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<RealConstDecl>(args[0]))
	      {
	          return new RealConstDecl(rc->Loc(), cos(rc->Value()));
	      }
	      return 0;
	  } },
	{ "ln", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<RealConstDecl>(args[0]))
	      {
	          // Yes, we want log here, log10 is Pascal type log
	          return new RealConstDecl(rc->Loc(), log(rc->Value()));
	      }
	      return 0;
	  } },
	{ "exp", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<RealConstDecl>(args[0]))
	      {
	          return new RealConstDecl(rc->Loc(), exp(rc->Value()));
	      }
	      return 0;
	  } },
	{ "frac", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<RealConstDecl>(args[0]))
	      {
	          double intpart;
	          return new RealConstDecl(rc->Loc(), modf(rc->Value(), &intpart));
	      }
	      return 0;
	  } },

	{ "int", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<RealConstDecl>(args[0]))
	      {
	          double r = rc->Value();
	          return new RealConstDecl(rc->Loc(), copysign(floor(std::abs(r)), r));
	      }
	      return 0;
	  } },

	{ "trunc", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<RealConstDecl>(args[0]))
	      {
	          return new IntConstDecl(rc->Loc(), (int64_t)(rc->Value()));
	      }
	      return 0;
	  } },

	{ "round", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<RealConstDecl>(args[0]))
	      {
	          return new IntConstDecl(rc->Loc(), (int64_t)(round(rc->Value())));
	      }
	      return 0;
	  } },

	{ "odd", 1, 1,
	  [](const ConstArgs& args) -> const ConstDecl*
	  {
	      if (auto rc = llvm::dyn_cast<IntConstDecl>(args[0]))
	      {
	          return new BoolConstDecl(rc->Loc(), rc->Value() & 1);
	      }
	      return 0;
	  } },
    };

    static EvaluableFunc* FindEvaluableFunc(std::string name)
    {
	strlower(name);
	auto func = std::find_if(evaluableFunctions.begin(), evaluableFunctions.end(),
	                         [&](auto it) { return name == it.name; });

	if (func != evaluableFunctions.end())
	{
	    return &*func;
	}
	return 0;
    }

    const ConstDecl* EvalFunction(const std::string& name, const std::vector<const ConstDecl*>& args)
    {
	EvaluableFunc* func = FindEvaluableFunc(name);
	if (func->minArgs > args.size() || func->maxArgs < args.size())
	{
	    std::stringstream ss;
	    ss << "Incorrect number of arguments for " << name << " expected " << func->minArgs;
	    if (func->maxArgs != func->minArgs)
	    {
		ss << ".." << func->maxArgs;
	    }
	    ss << " arguments.";
	    return ErrorConst(ss.str());
	}
	return func->func(args);
    }

    bool IsEvaluableFunc(const std::string& name)
    {
	return FindEvaluableFunc(name) != 0;
    }

    int64_t ToInt(const ConstDecl* c)
    {
	if (auto ci = llvm::dyn_cast<IntConstDecl>(c))
	{
	    return ci->Value();
	}
	if (auto cc = llvm::dyn_cast<CharConstDecl>(c))
	{
	    return cc->Value();
	}
	if (auto ce = llvm::dyn_cast<EnumConstDecl>(c))
	{
	    return ce->Value();
	}
	if (auto cb = llvm::dyn_cast<BoolConstDecl>(c))
	{
	    return cb->Value();
	}
	c->dump();
	ICE("Unknown constant type");
	return -1;
    }

    const ConstDecl* ToRealConstDecl(const ConstDecl* c)
    {
	if (llvm::isa<RealConstDecl>(c))
	{
	    return c;
	}
	if (auto ic = llvm::dyn_cast<IntConstDecl>(c))
	{
	    return new RealConstDecl(ic->Loc(), ic->Value());
	}
	return ErrorConst("Expected an integer constant");
    }

} // namespace Constants
