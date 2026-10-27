## Summary

<!-- What does this change and why? Link related issues, e.g. "Closes #12". -->

## How was it tested?

<!-- Unit tests, manual steps, screenshots for UI changes. -->

## Checklist

- [ ] Commits follow [Conventional Commits](https://www.conventionalcommits.org/) and are atomic
- [ ] Code is formatted with `clang-format`
- [ ] Tests added or updated, and `ctest --preset dev` passes
- [ ] No disk I/O added on the UI thread in a hot path
- [ ] Changes to saving/loading keep the durability guarantees
