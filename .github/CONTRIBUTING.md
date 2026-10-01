# Contributing

This document describes how you can contribute to AstraGram.

**Table of Contents**

* [What contributions are accepted](#what-contributions-are-accepted)
* [Build instructions](#build-instructions)
* [Keep your branch up to date](#keep-your-branch-up-to-date)
* [How to get your pull request accepted](#how-to-get-your-pull-request-accepted)
  * [Keep your pull requests limited to a single issue](#keep-your-pull-requests-limited-to-a-single-issue)
    * [Squash your commits to a single commit](#squash-your-commits-to-a-single-commit)
  * [Don't mix code changes with whitespace cleanup](#dont-mix-code-changes-with-whitespace-cleanup)
  * [Keep your code simple!](#keep-your-code-simple)
  * [Test your changes!](#test-your-changes)
  * [Write a good commit message](#write-a-good-commit-message)

## What contributions are accepted

We highly appreciate your contributions in the matter of fixing bugs and optimizing the AstraGram source code and its documentation. In case of fixing the existing user experience please push to your fork and [submit a pull request][pr].

Highly appreciated feature implementations from [Android app][android_repo].

## Build instructions

See [Building from Source][build_instructions] in the README for Windows, Linux
and macOS.

## Keep your branch up to date

Upstream means the official [Telegram Desktop][tdesktop] stable releases. Maintainers
merge each new stable release into this repository, so contributors never pull from
Telegram Desktop directly.

Before opening a pull request, bring your branch up to date with this repository's
`main` branch:

    git remote add plus https://github.com/Kindness-Kismet/AstraGram-Desktop.git
    git fetch plus main

Check the log to be sure that you actually want the changes, before rebasing:

    git log HEAD..plus/main

Then rebase your changes on top of it:

    git rebase plus/main

Rebasing rewrites your branch, so push it with `git push --force-with-lease`. Only do
this on your own pull request branch.

## How to get your pull request accepted

We want to improve AstraGram with your contributions. But we also want to provide a stable experience for our users and the community. Follow these rules and you should succeed without a problem!

### Keep your pull requests limited to a single issue

Pull requests should be as small/atomic as possible. Large, wide-sweeping changes in a pull request will be **rejected**, with comments to isolate the specific code in your pull request. Some examples:

* If you are making spelling corrections in the docs, don't modify other files.
* If you are adding new functions don't '*cleanup*' unrelated functions. That cleanup belongs in another pull request.

#### Squash your commits to a single commit

To keep the history of the project clean, you should make one commit per pull request.
If you already have multiple commits, you can add the commits together (squash them) with the following commands in Git Bash:

1. Open `Git Bash` (or `Git Shell`)
2. Enter following command to squash the recent {N} commits: `git reset --soft HEAD~{N} && git commit` (replace `{N}` with the number of commits you want to squash)
3. Press <kbd>i</kbd> to get into Insert-mode
4. Enter the commit message of the new commit
5. After adding the message, press <kbd>ESC</kbd> to get out of the Insert-mode
6. Write `:wq` and press <kbd>Enter</kbd> to save the new message or write `:q!` to discard your changes
7. Enter `git push --force` to push the new commit to the remote repository

For example, if you want to squash the last 5 commits, use `git reset --soft HEAD~5 && git commit`

### Don't mix code changes with whitespace cleanup

If you change two lines of code and correct 200 lines of whitespace issues in a file the diff on that pull request is functionally unreadable and will be **rejected**. Whitespace cleanups need to be in their own pull request.

### Keep your code simple!

Please keep your code as clean and straightforward as possible.
Furthermore, the pixel shortage is over. We want to see:

* `opacity` instead of `o`
* `placeholder` instead of `ph`
* `myFunctionThatDoesThings()` instead of `mftdt()`

### Test your changes!

Before you submit a pull request, please test your changes. Verify that AstraGram still works and your changes don't cause other issue or crashes.

### Write a good commit message

* Explain why you make the changes. [More infos about a good commit message.][commit_message]

* If you fix an issue with your commit, please close the issue by [adding one of the keywords and the issue number][closing-issues-via-commit-messages] to your commit message.

  For example: `Fix #545`

[//]: # (LINKS)
[tdesktop]: https://github.com/telegramdesktop/tdesktop
[commit_message]: http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html
[pr]: https://github.com/Kindness-Kismet/AstraGram-Desktop/compare
[build_instructions]: ../README.md#-building-from-source
[closing-issues-via-commit-messages]: https://help.github.com/articles/closing-issues-via-commit-messages/
[android_repo]: https://github.com/AyuGram/AyuGram4A
