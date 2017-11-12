// test.skub.cpp

// This is a file to test that skub can do what we want

/* [[[skub:
local gClasses = {};

function declareClass(name, base)
	cls = {}
	cls.name = name;
	cls.base = base;

	_G[name] = cls;
	table.insert(gClasses, cls);

	return cls;
end

declareClass("SyntaxNode");
declareClass("Expr", SyntaxNode);
declareClass("NameExpr", Expr);

for _,cls in pairs(gClasses) do `{{
class $(cls.name)${if cls.base then} : $(cls.base.name)${end}
{

};
}}end

]]] */

class SyntaxNode
{

};

class Expr : SyntaxNode
{

};

class NameExpr : Expr
{

};
// [[[end]]]

// after!
