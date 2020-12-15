package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

class Environment {
    private final Map<String, Object> values = new HashMap<>();
    final Environment enclosing;

    Environment() {
        enclosing = null;
    }

    Environment(Environment enclosing) {
        this.enclosing = enclosing;
    }

    void define(String name, Object value) {
        values.put(name, value);
    }

    Object get(Token name) {
        if (values.containsKey(name.lexeme)) {
            return values.get(name.lexeme);
        }
        
        // if we can't find it in the current environment, we delegate
        // this to the enclosing environment, if it fails to get it as well
        // it will eventually throw an error when we reach the top of the 
        // environment tree.
        if (enclosing != null) return enclosing.get(name);

        throw new RuntimeError(name, undefinedVarErrString(name.lexeme));
    }

	public void assign(Token name, Object value) {
        if (values.containsKey(name.lexeme)) {
            values.put(name.lexeme, value);
            return;
        }
        
        // if we failed to assign in this environment, try to do so in the
        // enclosing environments
        if (enclosing != null) {
            enclosing.assign(name, value);
            return;
        }
        
        throw new RuntimeError(name, undefinedVarErrString(name.lexeme));
    }
    
    private String undefinedVarErrString(String name) {
        return String.format("Undefined variable '%s'.", name);
    }

	public Object getAt(Integer distance, String name) {
        return ancestor(distance).values.get(name);
    }

    Environment ancestor(Integer distance) {
        Environment environment = this;
        for (int i = 0; i < distance; i++) {
            environment = environment.enclosing;
        }
        return environment;
    }

	public void assignAt(Integer distance, Token name, Object value) {
        ancestor(distance).values.put(name.lexeme, value);
	}
}
