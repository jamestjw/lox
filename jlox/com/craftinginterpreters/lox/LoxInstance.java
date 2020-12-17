package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

// Runtime representation of an instance of a class
class LoxInstance {
    private LoxClass klass;
    private final Map<String, Object> fields = new HashMap<>();

    LoxInstance(LoxClass klass) {
        this.klass = klass;
    }

    @Override
    public String toString() {
        return String.format("%s instance", klass.name);
    }

    // Search for a field of that name, failing which we search for a method of that name
	Object get(Token name) {
        if (fields.containsKey(name.lexeme)) {
            return fields.get(name.lexeme);
        }

        LoxFunction method = klass.findMethod(name.lexeme);
        if (method != null) return method.bind(this);

		throw new RuntimeError(name, String.format("Undefined property '%s'.", name.lexeme));
	}

	void set(Token name, Object value) {
        fields.put(name.lexeme, value);
	}
}
