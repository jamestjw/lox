# lox

## How to run?
### Java version
Run `GenerateAst` tool
```
cd jlox
javac com/craftinginterpreters/tool/*.java
java -cp . com.craftinginterpreters.tool.GenerateAst ./com/craftinginterpreters/lox/
```

Run interpreter.
```
cd jlox
javac com/craftinginterpreters/lox/*.java
java -cp . com.craftinginterpreters.lox.Lox
```

### C version
To be implemented