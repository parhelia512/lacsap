#include "semantics.h"
#include "expr.h"
#include "options.h"
#include "token.h"
#include "trace.h"
#include "visitor.h"

class TypeCheckVisitor : public ASTVisitor
{
public:
    TypeCheckVisitor(Source& src, Semantics* s) : sema(s), source(src){};
    void visit(ExprAST* expr) override;

private:
    Types::TypeDecl* BinarySetUpdate(BinaryExprAST* b);
    Types::TypeDecl* BinaryExprType(BinaryExprAST* b);
    template<typename T>
    void Check(T* t);
    template<typename T>
    void MaybeCheck(ExprAST* t);
    void Error(const ExprAST* e, const std::string& msg) const;

private:
    Semantics* sema;
    Source&    source;
};

template<typename T>
T* FindParentOfType(ExprAST* e)
{
    class CheckISA : public ASTVisitor
    {
    public:
	void visit(ExprAST* e) override
	{
	    if (auto t = llvm::dyn_cast<T>(e))
	    {
		if (!result)
		{
		    result = t;
		}
	    }
	}

	T* result = 0;
    };

    CheckISA ci;
    e->accept(ci);
    return ci.result;
}

class SemaFixup
{
public:
    SemaFixup() {}
    virtual void DoIt() = 0;
    virtual ~SemaFixup() {}
};

class SetRangeFixup : public SemaFixup
{
public:
    SetRangeFixup(SetExprAST* s, Types::RangeDecl* r) : expr(s), guessRange(r) {}
    void DoIt() override;

private:
    SetExprAST*       expr;
    Types::RangeDecl* guessRange;
};

void SetRangeFixup::DoIt()
{
    if (!expr->Type()->GetRange())
    {
	auto sd = llvm::dyn_cast<Types::SetDecl>(expr->Type());
	sd->UpdateRange(guessRange);
    }
}

static Types::RangeDecl* GetRangeDecl(Types::TypeDecl* ty)
{
    Types::Range*    r = ty->GetRange();
    Types::TypeDecl* base;
    auto             sty = llvm::dyn_cast<Types::SetDecl>(ty);
    if (!r)
    {
	ICE_IF(!sty, "Should be a set");
	if (!sty->SubType())
	{
	    return 0;
	}
	r = sty->SubType()->GetRange();
    }

    if (IsIntegral(ty))
    {
	base = ty;
    }
    else
    {
	base = sty->SubType();
    }

    if (r->Size() > Types::SetDecl::MaxSetSize)
    {
	r = new Types::Range(0, Types::SetDecl::MaxSetSize - 1);
    }

    return new Types::RangeDecl(r, base);
}

void TypeCheckVisitor::Error(const ExprAST* e, const std::string& msg) const
{
    std::cerr << e->Loc() << " Error: " << msg << std::endl;
    if (e->Loc())
    {
	source.PrintSource(e->Loc().LineNumber());
    }
    sema->AddError();
}

ExprAST* Recast(ExprAST* a, const Types::TypeDecl* ty)
{
    if (*ty != *a->Type())
    {
	ExprAST* e = a;
	a = new TypeCastAST(e->Loc(), e, ty);
    }
    return a;
}

Types::TypeDecl* TypeCheckVisitor::BinarySetUpdate(BinaryExprAST* b)
{
    auto lty = llvm::cast<Types::SetDecl>(b->lhs->Type());
    auto rty = llvm::cast<Types::SetDecl>(b->rhs->Type());
    ICE_IF(!lty || !rty, "Expect set declarations on both side!");
    if (auto s = llvm::dyn_cast<SetExprAST>(b->lhs))
    {
	if (s->values.empty() && rty->SubType())
	{
	    lty->UpdateSubtype(rty->SubType());
	}
    }
    if (auto s = llvm::dyn_cast<SetExprAST>(b->rhs))
    {
	if ((s->values.empty() || !rty->SubType()) && lty->SubType())
	{
	    rty->UpdateSubtype(lty->SubType());
	}
    }
    if (!lty->GetRange() && !rty->GetRange())
    {
	Types::RangeDecl* r = GetRangeDecl(Types::Get<Types::IntegerDecl>());

	if (!rty->SubType() && !lty->SubType())
	{
	    rty->UpdateSubtype(r->SubType());
	    lty->UpdateSubtype(r->SubType());
	}
	else
	{
	    r = GetRangeDecl(rty->SubType());
	}

	lty->UpdateRange(r);
	rty->UpdateRange(r);
    }

    if (!lty->GetRange() && rty->GetRange())
    {
	llvm::dyn_cast<Types::SetDecl>(lty)->UpdateRange(GetRangeDecl(rty));
    }
    if (!rty->GetRange() && lty->GetRange())
    {
	llvm::dyn_cast<Types::SetDecl>(rty)->UpdateRange(GetRangeDecl(lty));
    }

    Types::Range* lr = lty->GetRange();
    Types::Range* rr = rty->GetRange();

    if (*lty->SubType() != *rty->SubType())
    {
	Error(b, "Set type content isn't the same!");
    }
    else if (rr && lr && *rr != *lr)
    {
	Types::Range* range = new Types::Range(std::min(lr->Start(), rr->Start()),
	                                       std::max(lr->End(), rr->End()));

	Types::RangeDecl* r = new Types::RangeDecl(range, rty->SubType());
	Types::SetDecl*   set = new Types::SetDecl(r, rty->SubType());

	b->lhs = Recast(b->lhs, set);
	b->rhs = Recast(b->rhs, set);
    }
    return rty;
}

Types::TypeDecl* TypeCheckVisitor::BinaryExprType(BinaryExprAST* b)
{
    Types::TypeDecl* lty = b->lhs->Type();
    Types::TypeDecl* rty = b->rhs->Type();
    Types::TypeDecl* ty = 0;
    Token::TokenType op = b->oper.GetToken();

    ICE_IF(!rty || !lty, "Expect to have types here");

    if (op == Token::In)
    {
	if (!IsIntegral(lty))
	{
	    Error(b, "Left hand of 'in' expression should be integral type.");
	}

	// Empty set always has the "right" type
	if (auto s = llvm::dyn_cast<SetExprAST>(b->rhs))
	{
	    if (s->values.empty())
	    {
		llvm::dyn_cast<Types::SetDecl>(rty)->UpdateSubtype(lty);
	    }
	}
	if (auto sd = llvm::dyn_cast<Types::SetDecl>(rty))
	{
	    ICE_IF(!sd->SubType(), "Should have a subtype");
	    if (*lty != *sd->SubType())
	    {
		Error(b, "Left hand type does not match constituent parts of set");
	    }
	    if (!sd->GetRange())
	    {
		sd->UpdateRange(GetRangeDecl(lty));
	    }
	}
	else
	{
	    Error(b, "Right hand of 'in' expression should be a set.");
	}
	return Types::Get<Types::BoolDecl>();
    }

    if (llvm::isa<Types::SetDecl>(lty) && llvm::isa<Types::SetDecl>(rty))
    {
	ty = BinarySetUpdate(b);
	if (b->oper.IsCompare())
	{
	    return Types::Get<Types::BoolDecl>();
	}
	return ty;
    }

    if (b->oper.IsCompare())
    {
	if (llvm::isa<Types::ComplexDecl>(lty) && llvm::isa<Types::ComplexDecl>(rty))
	{
	    if (b->oper.GetToken() != Token::Equal && b->oper.GetToken() != Token::NotEqual)
	    {
		Error(b, "Only = and <> comparison allowed for complex types");
	    }
	    return Types::Get<Types::BoolDecl>();
	}
	if (llvm::isa<Types::StringDecl>(lty) || llvm::isa<Types::StringDecl>(rty))
	{
	    auto ty = Types::Get<Types::StringDecl>(255);
	    b->rhs = Recast(b->rhs, ty);
	    b->lhs = Recast(b->lhs, ty);
	    return Types::Get<Types::BoolDecl>();
	}
    }

    switch (op)
    {
    case Token::And_Then:
    case Token::Or_Else:
	if (!llvm::isa<Types::BoolDecl>(lty) || !llvm::isa<Types::BoolDecl>(rty))
	{
	    Error(b, "Types for And_Then and Or_Else should be boolean");
	}
	return Types::Get<Types::BoolDecl>();
	break;

    case Token::Plus:
	if (Types::IsStringLike(lty) && Types::IsStringLike(rty))
	{
	    return Types::Get<Types::StringDecl>(255);
	}
	break;

    case Token::Pow:
	if (llvm::isa<Types::ComplexDecl>(lty))
	{
	    if (!Types::IsIntegral(rty) || llvm::isa<Types::CharDecl>(rty))
	    {
		Error(b, "Expect integer exponent in POW");
	    }
	    ty = lty;
	    b->rhs = Recast(b->rhs, Types::Get<Types::RealDecl>());
	    return ty;
	}
	break;

    case Token::Divide:
	if (!Types::IsNumeric(lty) || !Types::IsNumeric(rty))
	{
	    Error(b, "Invalid (non-numeric) type for divide or power");
	}

	if (llvm::isa<Types::ComplexDecl>(lty) || llvm::isa<Types::ComplexDecl>(rty))
	{
	    ty = Types::Get<Types::ComplexDecl>();
	}
	else
	{
	    ty = Types::Get<Types::RealDecl>();
	}

	b->lhs = Recast(b->lhs, ty);
	lty = ty;

	b->rhs = Recast(b->rhs, ty);
	rty = ty;

	if (!lty->CompatibleType(rty))
	{
	    Error(b, "Incompatible type for divide");
	}
	return ty;

    case Token::Power:
	if (!Types::IsNumeric(lty) || !Types::IsNumeric(rty))
	{
	    Error(b, "Invalid (non-numeric) type for divide or power");
	}

	if (llvm::isa<Types::ComplexDecl>(lty))
	{
	    ty = Types::Get<Types::ComplexDecl>();
	}
	else
	{
	    ty = Types::Get<Types::RealDecl>();
	}

	b->lhs = Recast(b->lhs, ty);
	lty = ty;

	if (llvm::isa<Types::ComplexDecl>(rty))
	{
	    Error(b, "Exponent for ** operator should not be a complex value");
	}

	b->rhs = Recast(b->rhs, Types::Get<Types::RealDecl>());
	rty = Types::Get<Types::RealDecl>();

	if (!llvm::isa<Types::RealDecl, Types::ComplexDecl>(lty))
	{
	    Error(b, "Left hand side is wrong type (not possible to convert to real or complex)");
	}
	return ty;

    default:
	break;
    }

    if (((llvm::isa<Types::PointerDecl>(lty) && llvm::isa<NilExprAST>(b->rhs)) ||
         (llvm::isa<Types::PointerDecl>(rty) && llvm::isa<NilExprAST>(b->lhs))) &&
        (op == Token::Equal || op == Token::NotEqual))
    {
	if (llvm::isa<NilExprAST>(b->rhs))
	{
	    b->rhs = Recast(b->rhs, lty);
	    return lty;
	}

	b->lhs = Recast(b->lhs, rty);
	return rty;
    }

    if (llvm::isa<Types::RangeDecl>(lty) && llvm::isa<IntegerExprAST>(b->rhs))
    {
	return lty;
    }

    if (llvm::isa<Types::RangeDecl>(rty) && llvm::isa<IntegerExprAST>(b->lhs))
    {
	return rty;
    }

    return 0;
}

template<>
void TypeCheckVisitor::Check<BinaryExprAST>(BinaryExprAST* b)
{
    TRACE();

    Types::TypeDecl* ty = BinaryExprType(b);

    if (!ty)
    {
	Types::TypeDecl* lty = b->lhs->Type();
	Types::TypeDecl* rty = b->rhs->Type();
	Token::TokenType op = b->oper.GetToken();

	switch (op)
	{
	case Token::Pow:
	    if (!IsNumeric(lty))
	    {
		Error(b, "Left hand side of POW should be numeric type");
	    }
	    if (llvm::isa<Types::CharDecl>(rty) || !IsIntegral(rty))
	    {
		Error(b, "Right hand side of POW should be an integer");
	    }
	    break;

	case Token::Div:
	case Token::Mod:
	    if (llvm::isa<Types::CharDecl>(lty) || llvm::isa<Types::CharDecl>(rty) || !IsIntegral(lty) ||
	        !IsIntegral(rty))
	    {
		Error(b, "Types for DIV and MOD should be integer");
	    }
	    break;

	case Token::And:
	case Token::Xor:
	case Token::Or:
	    if (llvm::isa<Types::BoolDecl>(lty) && llvm::isa<Types::BoolDecl>(rty))
	    {
		ty = Types::Get<Types::BoolDecl>();
	    }
	    if (!IsIntegral(lty) || !IsIntegral(rty))
	    {
		Error(b, "Expression must be integral types on both sides");
	    }
	    if (llvm::isa<Types::CharDecl>(lty) || llvm::isa<Types::CharDecl>(rty))
	    {
		Error(b, "Types for binary operation should not be CHARACTER");
	    }
	    break;

	case Token::Minus:
	case Token::Multiply:
	case Token::Plus:
	    if (!IsNumeric(lty) || !IsNumeric(rty))
	    {
		Error(b, "Expression must be numeric types on both sides");
	    }
	    if (llvm::isa<Types::CharDecl>(lty) || llvm::isa<Types::CharDecl>(rty))
	    {
		Error(b, "Types for binary operation should not be CHARACTER");
	    }
	    break;

	default:
	    break;
	}
	if (!ty)
	{
	    if ((ty = const_cast<Types::TypeDecl*>(lty->CompatibleType(rty))))
	    {
		if (!IsCompound(ty))
		{
		    b->lhs = Recast(b->lhs, ty);
		    b->rhs = Recast(b->rhs, ty);
		}
	    }
	    else
	    {
		Error(b, "Incompatible type in expression");
	    }
	}
    }
    b->UpdateType(ty);
}

template<>
void TypeCheckVisitor::Check<UnaryExprAST>(UnaryExprAST* u)
{
    TRACE();

    Types::TypeDecl* ty = u->rhs->Type();
    if (u->oper.GetToken() == Token::Not)
    {
	if (!IsIntegral(ty) || llvm::isa<Types::CharDecl>(ty))
	{
	    Error(u, "Expect integral argument to NOT");
	}
    }
    if (u->oper.GetToken() == Token::Minus)
    {
	if (!IsNumeric(ty))
	{
	    Error(u, "Expect numeric type (Real, Integer) argument to unary '-'");
	}
    }
    u->UpdateType(ty);
}

template<>
void TypeCheckVisitor::Check<AssignExprAST>(AssignExprAST* a)
{
    TRACE();

    Types::TypeDecl* lty = a->lhs->Type();
    Types::TypeDecl* rty = a->rhs->Type();

    auto vExpr = FindParentOfType<VariableExprAST>(a->lhs);
    if (!vExpr)
    {
	Error(a, "Assigning to a constant");
	return;
    }

    if (vExpr->IsProtected())
    {
	Error(a, "Assigning to protected value");
	return;
    }

    auto lsty = llvm::dyn_cast<Types::SetDecl>(lty);
    auto rsty = llvm::dyn_cast<Types::SetDecl>(rty);
    if (lsty && rsty)
    {
	ICE_IF(!lsty->GetRange() || !lsty->SubType(), "Expected left type to be well defined.");

	if (!rsty->GetRange())
	{
	    rsty->UpdateRange(GetRangeDecl(lsty));
	}
	if (!rsty->SubType())
	{
	    rsty->UpdateSubtype(lsty->SubType());
	}
    }

    // Note difference to binary expression: only allowed on rhs!
    if (llvm::isa<Types::PointerDecl>(lty) && llvm::isa<NilExprAST>(a->rhs))
    {
	a->rhs = Recast(a->rhs, lty);
	return;
    }

    if (llvm::isa<Types::RangeDecl>(lty) && llvm::isa<IntegerExprAST>(a->rhs))
    {
	Types::Range* r = lty->GetRange();
	int64_t       v = llvm::dyn_cast<IntegerExprAST>(a->rhs)->Int();
	if (r->Start() > v || v > r->End())
	{
	    Error(a, "Value out of range");
	}
	return;
    }

    if (llvm::isa<Types::DynRangeDecl>(lty) && llvm::isa<IntegerExprAST>(a->rhs))
    {
	if (lty->Type() != rty->Type())
	{
	    Error(a, "Incompatible types");
	}
	return;
    }

    if (Types::IsCharArray(lty) && !llvm::isa<Types::StringDecl>(lty) && llvm::isa<StringExprAST>(a->rhs))
    {
	auto s = llvm::dyn_cast<StringExprAST>(a->rhs);
	auto aty = llvm::dyn_cast<Types::ArrayDecl>(lty);
	if (aty->Ranges().size() == 1)
	{
	    if (aty->Ranges()[0]->GetRange()->Size() >= s->Str().size())
	    {
		return;
	    }
	}
	Error(a, "String assignment from incompatible string constant");
	return;
    }

    const Types::TypeDecl* ty = lty->AssignableType(rty);
    if (!ty)
    {
	Error(a, "Incompatible type in assignment");
	return;
    }
    a->rhs = Recast(a->rhs, ty);
}

template<>
void TypeCheckVisitor::Check<RangeExprAST>(RangeExprAST* r)
{
    TRACE();

    if (*r->low->Type() != *r->high->Type())
    {
	Error(r, "Range should be same type at both ends");
    }
}

template<>
void TypeCheckVisitor::Check<SetExprAST>(SetExprAST* s)
{
    TRACE();

    Types::Range* r;
    if (!(r = s->Type()->GetRange()))
    {
	Types::RangeDecl* rd = GetRangeDecl(s->Type());
	auto              sd = llvm::dyn_cast<Types::SetDecl>(s->Type());
	if (sd->SubType())
	{
	    sema->AddFixup(new SetRangeFixup(s, rd));
	}
    }
}

template<>
void TypeCheckVisitor::Check<ArrayExprAST>(ArrayExprAST* a)
{
    TRACE();

    for (size_t i = 0; i < a->indices.size(); i++)
    {
	ExprAST* e = a->indices[i];
	if (!IsIntegral(e->Type()) || llvm::isa<RangeExprAST>(e))
	{
	    Error(e, "Index should be an integral type");
	}
	auto r = llvm::dyn_cast<Types::RangeDecl>(a->ranges[i]);
	if (llvm::isa<RangeReduceAST>(e))
	{
	    continue;
	}
	if (r->Type() != e->Type()->Type() && !e->Type()->CompatibleType(r))
	{
	    Error(a, "Incorrect index type");
	}
	if (rangeCheck)
	{
	    a->indices[i] = new RangeCheckAST(e, r);
	}
	else
	{
	    a->indices[i] = new RangeReduceAST(e, r);
	}
    }
}

template<>
void TypeCheckVisitor::Check<DynArrayExprAST>(DynArrayExprAST* d)
{
    TRACE();

    ExprAST* e = d->index;
    if (!IsIntegral(e->Type()) || llvm::isa<RangeExprAST>(e))
    {
	Error(e, "Index should be an integral type");
    }
    Types::DynRangeDecl* r = d->range;
    if (llvm::isa<RangeReduceAST>(e))
	return;
    if (r->Type() != e->Type()->Type() && !e->Type()->CompatibleType(r))
    {
	Error(d, "Incorrect index type");
    }
    if (rangeCheck)
    {
	d->index = new RangeCheckAST(e, r);
    }
    else
    {
	d->index = new RangeReduceAST(e, r);
    }
}

template<>
void TypeCheckVisitor::Check<BuiltinExprAST>(BuiltinExprAST* b)
{
    TRACE();
    Builtin::ErrorType e = b->bif->Semantics();
    switch (e)
    {
    case Builtin::ErrorType::WrongArgCount:
	Error(b, "Builtin function: '" + b->bif->Name() + "' wrong number of arguments");
	break;
    case Builtin::ErrorType::WrongArgType:
	Error(b, "Builtin function: '" + b->bif->Name() + "' wrong argument type(s)");
	break;
    case Builtin::ErrorType::Ok:
	break;
    }
}

template<>
void TypeCheckVisitor::Check<CallExprAST>(CallExprAST* c)
{
    TRACE();
    const PrototypeAST* proto = c->proto;
    if (c->args.size() != proto->args.size())
    {
	Error(c, "Incorrect number of arguments in call to " + c->proto->Name());
	return;
    }
    int                        idx = 0;
    const std::vector<VarDef>& parg = proto->args;
    for (auto& a : c->args)
    {
	bool bad = true;

	if (const Types::TypeDecl* ty = parg[idx].Type()->AssignableType(a->Type()))
	{
	    if (parg[idx].IsRef() && !llvm::isa<AddressableAST, ClosureAST>(a))
	    {
		Error(c, "Expect variable for 'var' parameter");
	    }
	    else
	    {
		a = Recast(a, ty);
		bad = false;
	    }
	}
	else if (llvm::isa<Types::PointerDecl>(parg[idx].Type()) && llvm::isa<NilExprAST>(a))
	{
	    a = Recast(a, parg[idx].Type());
	    bad = false;
	}
	else if (auto argTy = llvm::dyn_cast<Types::FuncPtrDecl>(parg[idx].Type()))
	{
	    auto fnArg = llvm::dyn_cast<FunctionExprAST>(a);
	    ICE_IF(!fnArg, "Expected argument to be FunctionExprAST");

	    if (fnArg->Proto()->IsMatchWithoutClosure(argTy->Proto()))
	    {
		// Todo: Make this a function
		std::vector<VariableExprAST*> vf;
		FunctionAST*                  fn = fnArg->Proto()->Function();
		Types::TypeDecl*              closureTy = fn->ClosureType();
		for (auto u : fn->UsedVars())
		{
		    vf.push_back(new VariableExprAST(fn->Loc(), u.Name(), u.Type()));
		}
		ClosureAST* closure = new ClosureAST(fn->Loc(), closureTy, vf);
		a = new TrampolineAST(fnArg->loc, fnArg, closure, argTy);
		bad = false;
	    }
	    else
	    {
		bad = !(*fnArg->Proto() == *argTy->Proto());
	    }
	}
	if (bad)
	{
	    Error(c, "Incompatible argument type " + std::to_string(idx));
	}
	idx++;
    }
}

template<>
void TypeCheckVisitor::Check<ForExprAST>(ForExprAST* f)
{
    TRACE();
    // Check start + end and cast if necessary. Fail if incompatible types.
    Types::TypeDecl* vty = f->variable->Type();
    bool             bad = !IsIntegral(vty) && f->end;
    if (bad)
    {
	Error(f->variable, "Loop iteration variable must be integral type");
	return;
    }
    if (f->end)
    {
	if (const Types::TypeDecl* ty = f->start->Type()->AssignableType(vty))
	{
	    f->start = Recast(f->start, ty);
	}
	else
	{
	    bad = true;
	}
	if (const Types::TypeDecl* ty = f->end->Type()->AssignableType(vty))
	{
	    f->end = Recast(f->end, ty);
	}
	else
	{
	    bad = true;
	}
    }
    // No end = for x in set
    else
    {
	if (auto setDecl = llvm::dyn_cast<Types::SetDecl>(f->start->Type()))
	{
	    if (!setDecl->SubType()->CompatibleType(vty))
	    {
		Error(f->variable, "Expected variable to be compatible with set");
	    }
	}
	else if (auto arrDecl = llvm::dyn_cast<Types::ArrayDecl>(f->start->Type()))
	{
	    if (arrDecl->Ranges().size() != 1)
	    {
		Error(f->start, "Expected signle dimension array");
	    }
	    if (!arrDecl->SubType()->CompatibleType(vty))
	    {
		Error(f->variable, "Expected loop variable to be compatible with array");
	    }
	}
	else
	{
	    Error(f->start, "For in only available for array or set");
	}
    }
    if (bad)
    {
	Error(f, "Bad for loop");
    }
}

template<>
void TypeCheckVisitor::Check<ReadAST>(ReadAST* r)
{
    bool isText = r->kind == ReadAST::ReadKind::ReadStr || llvm::isa<Types::TextDecl>(r->src->Type());

    if (r->kind == ReadAST::ReadKind::ReadStr)
    {
	if (!llvm::isa<Types::StringDecl>(r->src->Type()))
	{
	    Error(r->src, "First argument to ReadStr should be a string");
	}
    }
    else
    {
	if (!llvm::isa<Types::FileDecl>(r->src->Type()))
	{
	    Error(r->src, "First argument to Read or ReadLn should be a file");
	}
	if (!isText && r->kind == ReadAST::ReadKind::ReadLn)
	{
	    Error(r->src, "File argument for ReadLn should be a textfile");
	}
    }

    if (isText)
    {
	for (auto arg : r->args)
	{
	    if (!llvm::isa<AddressableAST>(arg))
	    {
		Error(arg, "Invalid argument for Read/ReadLN - must be a variable-expression");
	    }
	    if (IsCompound(arg->Type()))
	    {
		bool bad = true;
		if (llvm::isa<Types::ArrayDecl>(arg->Type()))
		{
		    bad = !Types::IsCharArray(arg->Type()) && !llvm::isa<Types::StringDecl>(arg->Type());
		}
		if (bad)
		{
		    Error(arg, "Read argument must be simple type [or array of char NYI] or string");
		}
	    }
	}
    }
    else
    {
	if (r->args.size() != 1)
	{
	    Error(r, "Read of binary file must have exactly one argument after the file");
	}
	else
	{
	    ExprAST* arg = r->args[0];
	    if (!llvm::isa<VariableExprAST>(arg))
	    {
		Error(arg, "Invalid argument for read - must be a variable-expression");
	    }
	    else
	    {
		auto fd = llvm::dyn_cast<Types::FileDecl>(r->src->Type());
		if (!fd || !fd->SubType()->AssignableType(arg->Type()))
		{
		    Error(arg, "Read argument should match elements of the file");
		}
	    }
	}
    }
}

template<>
void TypeCheckVisitor::Check<WriteAST>(WriteAST* w)
{
    TRACE();
    bool isText = llvm::isa<Types::TextDecl>(w->dest->Type()) || w->kind == WriteAST::WriteKind::WriteStr;

    if (w->kind == WriteAST::WriteKind::WriteStr)
    {
	if (!llvm::isa<Types::StringDecl>(w->dest->Type()))
	{
	    Error(w->dest, "First argument to ReadStr should be a string");
	}
    }
    else
    {
	if (!llvm::isa<Types::FileDecl>(w->dest->Type()))
	{
	    Error(w->dest, "First argument to Write or WriteLn should be a file");
	}
	if (!isText && w->kind == WriteAST::WriteKind::WriteLn)
	{
	    Error(w->dest, "File argument for WritelLn should be a textfile");
	}
    }
    if (isText)
    {
	for (auto arg : w->args)
	{
	    ExprAST* e = arg.expr;
	    if (IsCompound(e->Type()))
	    {
		bool bad = true;
		if (IsCharArray(e->Type()))
		{
		    bad = false;
		}
		else
		{
		    bad = !llvm::isa<Types::StringDecl>(e->Type());
		}
		if (bad)
		{
		    Error(e, "Write argument must be simple type or array of char or string");
		}
	    }
	    else if (arg.precision)
	    {
		if (!llvm::isa<Types::RealDecl>(e->Type()))
		{
		    Error(e, "Unexpected precision argument when argument-type is not real");
		}
		else if (!llvm::isa<Types::IntegerDecl>(arg.precision->Type()))
		{
		    Error(e, "Precision field should be integer.");
		}
	    }
	    if (arg.width && !llvm::isa<Types::IntegerDecl>(arg.width->Type()))
	    {
		Error(e, "Width field should be an integer");
	    }
	}
    }
    else
    {
	if (w->args.size() != 1 || w->args[0].width || w->args[0].precision)
	{
	    Error(w, "Write of binary file must have exactly one argument");
	}
	else
	{
	    ExprAST* arg = w->args[0].expr;
	    auto     fd = llvm::dyn_cast<Types::FileDecl>(w->dest->Type());
	    if (!fd || !fd->SubType()->AssignableType(arg->Type()))
	    {
		Error(arg, "Write argument should match elements of the file");
	    }
	}
    }
}

template<>
void TypeCheckVisitor::Check<CaseExprAST>(CaseExprAST* c)
{
    TRACE();
    if (!IsIntegral(c->expr->Type()))
    {
	Error(c, "Case selection must be integral type");
    }

    std::vector<std::pair<int, int>> vals;
    for (auto l : c->labels)
    {
	for (auto i : l->labelValues)
	{
	    if (std::find(vals.begin(), vals.end(), i) != vals.end())
	    {
		Error(c, "Duplicate case label " + std::to_string(i.first));
	    }
	    vals.push_back(i);
	}
    }
}

template<>
void TypeCheckVisitor::Check(WhileExprAST* w)
{
    TRACE();
    if (!llvm::isa<Types::BoolDecl>(w->cond->Type()))
    {
	Error(w->cond, "The condition for 'while' should be a boolean expression");
    }
}

template<>
void TypeCheckVisitor::Check(RepeatExprAST* r)
{
    TRACE();
    if (!llvm::isa<Types::BoolDecl>(r->cond->Type()))
    {
	Error(r->cond, "The condition for 'repeat' should be a boolean expression");
    }
}

template<>
void TypeCheckVisitor::Check(IfExprAST* i)
{
    TRACE();
    if (!llvm::isa<Types::BoolDecl>(i->cond->Type()))
    {
	Error(i->cond, "The condition for 'if' should be a boolean expression");
    }
}

template<>
void TypeCheckVisitor::Check(InitArrayAST* a)
{
    std::vector<int64_t> indices;
    auto                 CheckDups = [&](int64_t x)
    {
	if (std::find(indices.begin(), indices.end(), x) != indices.end())
	{
	    Error(a, "Duplicate initializer " + std::to_string(x));
	}
    };
    bool hasOtherwise = false;

    for (auto v : a->values)
    {
	switch (v.Kind())
	{
	case ArrayInit::InitKind::Range:
	    for (int64_t i = v.Start(); i <= v.End(); i++)
	    {
		CheckDups(i);
		indices.push_back(i);
	    }
	    break;
	case ArrayInit::InitKind::Single:
	    CheckDups(v.Start());
	    indices.push_back(v.Start());
	    break;
	case ArrayInit::InitKind::Otherwise:
	    if (hasOtherwise)
	    {
		Error(a, "More than one otherwise in initializer");
	    }
	    hasOtherwise = true;
	    break;
	default:
	    llvm_unreachable("Unexpected initalizer kind");
	}
    }
}

void Semantics::AddFixup(SemaFixup* f)
{
    TRACE();
    fixups.push_back(f);
}

void Semantics::RunFixups()
{
    TRACE();

    for (auto f : fixups)
    {
	f->DoIt();
    }
}

template<typename T>
void TypeCheckVisitor::MaybeCheck(ExprAST* e)
{
    if (T* t = llvm::dyn_cast<T>(e))
    {
	Check(t);
    }
}

void TypeCheckVisitor::visit(ExprAST* expr)
{
    TRACE();

    if (verbosity > 1)
    {
	expr->dump();
    }

    MaybeCheck<BinaryExprAST>(expr);
    MaybeCheck<UnaryExprAST>(expr);
    MaybeCheck<AssignExprAST>(expr);
    MaybeCheck<RangeExprAST>(expr);
    MaybeCheck<SetExprAST>(expr);
    MaybeCheck<ArrayExprAST>(expr);
    MaybeCheck<DynArrayExprAST>(expr);
    MaybeCheck<BuiltinExprAST>(expr);
    MaybeCheck<CallExprAST>(expr);
    MaybeCheck<ForExprAST>(expr);
    MaybeCheck<ReadAST>(expr);
    MaybeCheck<WriteAST>(expr);
    MaybeCheck<CaseExprAST>(expr);
    MaybeCheck<WhileExprAST>(expr);
    MaybeCheck<RepeatExprAST>(expr);
    MaybeCheck<IfExprAST>(expr);
    MaybeCheck<InitArrayAST>(expr);
}

void Semantics::Analyse(Source& src, ExprAST* ast)
{
    TIME_TRACE();
    TRACE();

    TypeCheckVisitor tc(src, this);
    ast->accept(tc);
    RunFixups();
}
