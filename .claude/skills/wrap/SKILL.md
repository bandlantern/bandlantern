---
name: wrap
description: End-of-session wrap-up. Tick progress, close the issue, update CURRENT TASK, commit.
---

Wrap up the session:

1. Check `tools/tt status`. If a timer is still running, tell me to run `tools/tt stop "<note>"`
   first and wait.
2. In `docs/learning/CURRICULUM.md`, tick the module, milestone, or plumbing items finished this
   session (check `git log` and `git status`).
3. For each ticked item, find its issue with `gh issue list -l now` and close it with
   `gh issue close <n> -c "<commit or one line>"`.
4. Update the `CURRENT TASK:` line at the bottom of `CLAUDE.md` to the next open item.
5. Commit the changes (one line message). Don't push.

Don't summarize the session.
