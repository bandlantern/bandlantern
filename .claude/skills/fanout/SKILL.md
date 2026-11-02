---
name: fanout
description: Run independent pieces of a slice in parallel as clean-room-dev agents, each in its own worktree.
argument-hint: <contract file> <piece 1>; <piece 2>; ...
---

Fan out: $ARGUMENTS

1. Check that the contract file exists and is committed (`git status --short <file>`). If not, stop:
   the contract comes first.
2. Check that the pieces are independent: no shared files, each one testable on its own (e.g. the
   browser piece uses a fake frame stream, not the real engine). If two overlap, say so and propose a split.
3. Launch one `clean-room-dev` agent per piece with `isolation: "worktree"`, all in the same message.
   Each prompt names: the contract file, the piece's directories (and which it must not touch), the
   specs to work from, the test that defines done, and the 10-line report format.
4. While they run, don't repeat their work. When they report, list each branch, its test status, and
   any contract gaps. Then propose the merge order and the integration test to run. Merge only after
   I confirm.
