# Contributing to Vivi

> [!NOTE]
> Submissions from people outside of our discord community are currently closed and PR's will be rejected.
> 
> We'll open up restrictions on this as time goes on and we exit alpha.

Please always remember that Vivi is inteaded to be a hackable, modern metaprogramming language. The developer experience overwrites maintainer/creator experience, and anything that can be modified by users should always be permissable. Our job is to just make sure the language works as intended, not dictate best practices.

Everything about Vivi is a work in progress and you're encouraged to experiment. This file, as well as all policies, rules, and licenses are subject to change as time goes on.

## AI Policy

**No drive by AI bug fixes**. Period. Understand the code you propose and test it before submission.

For transparency, Claude is currently being used to generate test code, hunt for bugs, and automate menial tasks. LLM's will not be used for backend code generation, and we'd like to keep it that way.

## Blind firing & naming conventions

Don't blind fire large commits to the repo. Make a pull request as a proposal for other people to weigh in on and check over for consistency with the goal of the language. 

In the standard library, avoid common naming conventions typically used by devs. This means no generic names for things that aren't standard machinery, like print().

Also keep naming conventions consistent, and opt for leading group names. Eg: group_function, group_function2 etc. Don't start using camelCase without proposing a whole rewrite to the standard library and *why* you think that's better.

## Opt for Vivi implementation over C++ where possible

If something can be added as raw Vivi, always opt to do so instead of altering the backend. The goal is eventual bootstrapping, so as much existing Vivi code that can carry over during that step is ideal. Backend changes should be for additional language facilities that can't exist purely in Vivi, and for compiler target work.

Standard library functions should always be able to be derived from a simple builtin surface, so be attentive to not add any unnecessary single use machinery if possible. Use your best possible judgement for how to sort your additions to the standard library.

## Make library definitions mutable

Mutable definitions are currently the convention for shadowing. This means opting for := over :: where applicable. Mutable standard library definitions is in keeping with the language being hackable, even the _base.vivi library can be replaced if a developer chooses.

## Keep code simple

Follow the KISS principle or whatever else you want to fill in here to say less work is better work.

When working in the front end, heavy use of composition and concatenation is encouraged. When working on the compiler, keeping the binary's filesize down is encouraged. Avoid heavy C++ imports, and opt for C style code with C++ sugar as it's simpler and more readable/maintainable.

If your code works, it's a good code.

## Followup on changes

If you update the standard library, please take care to follow up on examples and update them as well if needed. Please also provide comments for explanation and attribution when working in the backend. This also means following up on discussions in the issues and pull request tabs.
