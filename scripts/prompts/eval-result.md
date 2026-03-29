# EvalResult Prompt Template

## Type Definition

```pascal
type
  Verdict = (Pass, Fail, Unknown);
  EvalResult = record
    verdict : Verdict;
    reasoning : String;
  end;
```

## Prompt

You are producing data in the Honest interchange format. Given the type definition above, produce a complete Honest document containing a variable named 'result' of type EvalResult. The verdict should be Pass, Fail, or Unknown based on your evaluation. The reasoning field should explain your verdict.

Output the complete Honest document including both the type declarations and the var section. Use single quotes for strings with '' to escape embedded quotes.

## Expected Output Pattern

```pascal
type
  Verdict = (Pass, Fail, Unknown);
  EvalResult = record
    verdict : Verdict;
    reasoning : String;
  end;

var result : EvalResult = (
  verdict : Pass;
  reasoning : 'Your reasoning here';
);
```
