package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Stack;

import com.craftinginterpreters.lox.Expr.Assign;
import com.craftinginterpreters.lox.Expr.Binary;
import com.craftinginterpreters.lox.Expr.Call;
import com.craftinginterpreters.lox.Expr.Get;
import com.craftinginterpreters.lox.Expr.Grouping;
import com.craftinginterpreters.lox.Expr.Literal;
import com.craftinginterpreters.lox.Expr.Logical;
import com.craftinginterpreters.lox.Expr.Set;
import com.craftinginterpreters.lox.Expr.Super;
import com.craftinginterpreters.lox.Expr.This;
import com.craftinginterpreters.lox.Expr.Unary;
import com.craftinginterpreters.lox.Expr.Variable;
import com.craftinginterpreters.lox.Stmt.Block;
import com.craftinginterpreters.lox.Stmt.Class;
import com.craftinginterpreters.lox.Stmt.Expression;
import com.craftinginterpreters.lox.Stmt.Function;
import com.craftinginterpreters.lox.Stmt.If;
import com.craftinginterpreters.lox.Stmt.Print;
import com.craftinginterpreters.lox.Stmt.Return;
import com.craftinginterpreters.lox.Stmt.Var;
import com.craftinginterpreters.lox.Stmt.While;

class Resolver implements Expr.Visitor<Void>, Stmt.Visitor<Void> {
    private final Interpreter interpreter;
    private final Stack<Map<String, Boolean>> scopes = new Stack<>();
    private FunctionType currentFunction = FunctionType.NONE;
    private ClassType currentClass = ClassType.NONE;

    Resolver(Interpreter interpreter) {
        this.interpreter = interpreter;
    }

    private enum FunctionType {
        NONE, FUNCTION, METHOD, INITIALIZER
    }

    private enum ClassType {
        NONE, CLASS, SUBCLASS
    }

    @Override
    public Void visitExpressionStmt(Expression stmt) {
        resolve(stmt.expression);
        return null;
    }

    @Override
    public Void visitIfStmt(If stmt) {
        resolve(stmt.condition);
        resolve(stmt.thenBranch);

        if (stmt.elseBranch != null)
            resolve(stmt.elseBranch);
        return null;
    }

    @Override
    public Void visitPrintStmt(Print stmt) {
        resolve(stmt.expression);
        return null;
    }

    @Override
    public Void visitVarStmt(Var stmt) {
        declare(stmt.name);
        if (stmt.initializer != null) {
            resolve(stmt.initializer);
        }
        define(stmt.name);
        return null;
    }

    @Override
    public Void visitBlockStmt(Block stmt) {
        beginScope();
        resolve(stmt.statements);
        endScope();
        return null;
    }

    @Override
    public Void visitWhileStmt(While stmt) {
        resolve(stmt.condition);
        resolve(stmt.body);
        return null;
    }

    @Override
    public Void visitFunctionStmt(Function stmt) {
        // We declare and define the function name first before resolving
        // it, this allows the same function to call itself recursively.
        declare(stmt.name);
        define(stmt.name);

        resolveFunction(stmt, FunctionType.FUNCTION);
        return null;
    }

    @Override
    public Void visitReturnStmt(Return stmt) {
        // Check if the return statement is within a function or a method
        if (currentFunction == FunctionType.NONE) {
            Lox.error(stmt.keyword, "Can't return from top-level code.");
        }

        if (stmt.value != null) {
            if (currentFunction == FunctionType.INITIALIZER) {
                Lox.error(stmt.keyword, "Can't return from an initializer.");
            }
            resolve(stmt.value);
        }

        return null;
    }

    @Override
    public Void visitAssignExpr(Assign expr) {
        resolve(expr.value);
        resolveLocal(expr, expr.name);
        return null;
    }

    @Override
    public Void visitBinaryExpr(Binary expr) {
        resolve(expr.left);
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitGroupingExpr(Grouping expr) {
        resolve(expr.expression);
        return null;
    }

    @Override
    public Void visitLiteralExpr(Literal expr) {
        return null;
    }

    @Override
    public Void visitUnaryExpr(Unary expr) {
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitVariableExpr(Variable expr) {
        if (!scopes.isEmpty() && scopes.peek().get(expr.name.lexeme) == Boolean.FALSE) {
            Lox.error(expr.name, "Can't read local variable in its own initializer.");
        }

        resolveLocal(expr, expr.name);

        return null;
    }

    @Override
    public Void visitLogicalExpr(Logical expr) {
        resolve(expr.left);
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitCallExpr(Call expr) {
        resolve(expr.callee);
        for (Expr argument : expr.arguments) {
            resolve(argument);
        }
        return null;
    }

    @Override
    public Void visitClassStmt(Class stmt) {
        ClassType enclosingClass = currentClass;
        currentClass = ClassType.CLASS;

        declare(stmt.name);
        define(stmt.name);

        // Throw error on resolution of class that inherits from self.
        if (stmt.superclass != null && stmt.name.lexeme.equals(stmt.superclass.name.lexeme)) {
            Lox.error(stmt.superclass.name, "A class can't inherit from itself.");
        }

        if (stmt.superclass != null) {
            currentClass = ClassType.SUBCLASS;
            resolve(stmt.superclass);
        }

        if (stmt.superclass != null) {
            beginScope();
            scopes.peek().put("super", true);
        }

        beginScope();
        scopes.peek().put("this", true);

        for (Stmt.Function method : stmt.methods) {
            FunctionType declaration = FunctionType.METHOD;
            if (method.name.lexeme.equals("init")) {
                declaration = FunctionType.INITIALIZER;
            }
            resolveFunction(method, declaration);
        }

        endScope();

        if (stmt.superclass != null)
            endScope();

        currentClass = enclosingClass;
        return null;
    }

    // Since properties are looked up dynamically, we cannot resolve them.
    @Override
    public Void visitGetExpr(Get expr) {
        resolve(expr.object);
        return null;
    }

    // Since properties are looked up dynamically, we just resolve the value and
    // the object
    @Override
    public Void visitSetExpr(Set expr) {
        resolve(expr.value);
        resolve(expr.object);
        return null;
    }

    @Override
    public Void visitThisExpr(This expr) {
        if (currentClass == ClassType.NONE) {
            Lox.error(expr.keyword, "Can't use 'this' outside of a class.");
            return null;
        }

        resolveLocal(expr, expr.keyword);
        return null;
    }

    @Override
    public Void visitSuperExpr(Super expr) {
        if (currentClass == ClassType.NONE) {
            Lox.error(expr.keyword, "Can't use 'super' outside of a class.");
        } else if (currentClass != ClassType.SUBCLASS) {
            Lox.error(expr.keyword, "Can't use 'super' in a class with no superclass.");
        }

        resolveLocal(expr, expr.keyword);
        return null;
    }

    void resolve(List<Stmt> statements) {
        for (Stmt statement : statements) {
            resolve(statement);
        }
    }

    private void resolve(Stmt statement) {
        statement.accept(this);
    }

    private void resolve(Expr expr) {
        expr.accept(this);
    }

    private void beginScope() {
        scopes.push(new HashMap<String, Boolean>());
    }

    private void endScope() {
        scopes.pop();
    }

    private void define(Token name) {
        if (scopes.isEmpty())
            return;

        // Mark it as fully initialized and ready for use.
        scopes.peek().put(name.lexeme, true);
    }

    private void declare(Token name) {
        if (scopes.isEmpty())
            return;
        Map<String, Boolean> scope = scopes.peek();

        // Throw an error if the user declares the same variable
        // twice in a particular scope
        if (scope.containsKey(name.lexeme)) {
            Lox.error(name, "A variable with this name has already been declared in this scope.");
        }

        // Mark it as not ready yet, i.e. whether or not we have resolved
        // the initializer
        scope.put(name.lexeme, false);
    }

    // Suppose that we never find the variable in any scope, we then assume
    // it must be defined in the global scope
    private void resolveLocal(Expr expr, Token name) {
        for (int i = scopes.size() - 1; i >= 0; i--) {
            if (scopes.get(i).containsKey(name.lexeme)) {
                // Passing in the number of scopes between the current innermost scope and
                // the scope in which the variable was found.
                interpreter.resolve(expr, scopes.size() - 1 - i);
                return;
            }
        }
    }

    // Create a new scope, then define all parameters before resolving the body.
    //
    // Note the difference between this and the interpreter, the interpreter
    // ignores the body during function definition, but static analysis will'
    // go all the way and analyse the body too.
    private void resolveFunction(Function stmt, FunctionType type) {
        FunctionType enclosingFunction = currentFunction;
        currentFunction = type;

        beginScope();
        for (Token param : stmt.params) {
            declare(param);
            define(param);
        }

        resolve(stmt.body);
        endScope();

        currentFunction = enclosingFunction;
    }
}
