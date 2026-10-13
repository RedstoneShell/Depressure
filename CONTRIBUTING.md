# Contributing to Depressure

Thank you for your interest in contributing to **Depressure** — a UEFI co-processor OS with FAT32+ELF support, full components access speed, stable task killing at errors, and more.

Before we can accept your contribution, you must sign our **Contributor License Agreement (CLA)**. This is a legal requirement that protects both you as a contributor and the project itself. The process is simple and described below.

---

## Table of Contents

- [Before You Start](#before-you-start)
- [How to Sign the CLA](#how-to-sign-the-cla)
- [Where to Send Your Signed CLA](#where-to-send-your-signed-cla)
- [What Happens After You Send It](#what-happens-after-you-send-it)
- [Contribution Guidelines](#contribution-guidelines)
- [How to Submit a Contribution](#how-to-submit-a-contribution)
- [Questions](#questions)

---

## Before You Start

1. Make sure you have read the CLA file in this repository: [`CLA`](./LICENSE.CLA)
2. Make sure you have a GitHub account (required for the `GitHub username` field).
3. Make sure you can send email from the address you want to register with us.

You do **not** need to print, scan, or physically sign anything. The CLA is signed electronically by copying the file, filling in your details, and emailing a screenshot.

---

## How to Sign the CLA

Signing the CLA takes only a few minutes. Follow these steps exactly:

### Step 1 — Download / copy the CLA

Open the file [`CLA`](./LICENSE.CLA) in this repository.

Copy the **entire contents** of the file into a new text document (or directly into your email client if you prefer).

> **Important:** Do not modify any part of the CLA text itself. You are only allowed to change the **bottom signature block**.

### Step 2 — Fill in the signature block

At the bottom of the copied CLA, you will find a block that looks like this:
```text
8. Signature

By signing below, You accept and agree to the terms and conditions of
this Contributor License Agreement for Your present and future
Contributions submitted to the Project.

Full name: Liutak Maksum

GitHub username: RedstoneShell

Email: depressuresupport@gmail.com

Address: Address was hidden for safery

Country: Ukraine

Signature: RSDevlopment

Date: 2026.10.13
```


Replace each filled line with **your own information**:

| Field | What to write | Example |
|---|---|---|
| **Full name** | Your real full legal name | `Ivan Petrenko` |
| **GitHub username** | Your GitHub handle (without `@`) | `ivan-dev` |
| **Email** | The email address you will send from | `ivan@example.com` |
| **Address** | Your postal address (city, street, building) | `Kyiv, Khreshchatyk St. 1, apt. 5` |
| **Country** | Your country of residence | `Ukraine` |
| **Signature** | Use the format `/s/ Your Full Name` | `/s/ Ivan Petrenko` |
| **Date** | Today's date in `YYYY-MM-DD` format | `2026-10-13` |

**Rules:**
- The `Signature` line must match your `Full name`.
- Use the `YYYY-MM-DD` date format to avoid confusion.
- Do not leave any field empty.
- If you are signing on behalf of a company, add a line `Company: Your Company Name` and `Title: Your Position` below the `Country` line.

### Step 3 — Take a screenshot

Take a clear screenshot of the **completed signature block** (the bottom part of the CLA with your information filled in).

The screenshot must show:
- The full signature block with all fields filled in.
- The heading `8. Signature` visible above it, so it is clear this is the CLA signature section.

Save the screenshot as `CLA_<your-github-username>.png` (for example `CLA_ivan-dev.png`).

> **Tip:** If you cannot take a screenshot, you may instead attach the full completed CLA text file. A screenshot is preferred, but a text file is also accepted.

### Step 4 — Send the email

Send an email with your screenshot attached to:

**depressuresupport@gmail.com**

Use the subject line:
```text
Depressure CLA - <your-github-username>
```

Example subject:
```text
Depressure CLA - ivan-dev
```

In the body of the email, briefly state:
```text
I, <Full Name>, agree to the terms of the Depressure Contributor
License Agreement. My GitHub username is <username>.
```


Attach the screenshot from Step 3.

---

## Where to Send Your Signed CLA

All signed CLAs must be sent to:

> **depressuresupport@gmail.com**

Do **not** open a GitHub issue or pull request just to submit your CLA. The CLA is handled privately by email so that your personal information (address, etc.) is not published publicly on GitHub.

---

## What Happens After You Send It

1. A maintainer will review your email within a few days.
2. If everything is correct, your CLA will be recorded and you will receive a confirmation reply.
3. Once confirmed, you may open pull requests and they will be accepted (provided they meet the contribution guidelines below).
4. If something is missing or incorrect, you will receive an email asking you to fix it and resend.

You only need to sign the CLA **once**. It covers all your present and future contributions to Depressure.

---

## Contribution Guidelines

To keep the project clean and reviewable, please follow these rules:

### Code
- Write clear, readable code. Prefer clarity over cleverness.
- Keep the existing code style of the repository (naming, indentation, file layout).
- Do not reformat unrelated files in the same pull request.
- Keep pull requests focused on a single change or feature.

### Commits
- Use descriptive commit messages (for example: `fix: correct FAT32 cluster chain walk`).
- Avoid commits like `update`, `fix`, `asdf`.
- Squash trivial commits before opening a pull request if possible.

### Tests
- If you add a new feature, describe how it was tested.
- If your change fixes a bug, describe how to reproduce the original bug.

### Licensing
- All contributions must be your own original work, or you must have the right to submit them.
- If you include third-party code, clearly state its source and license in your pull request description and in the file itself.
- By submitting a pull request, you confirm that you have signed the CLA.

### What We Will Not Accept
- Code that violates the CLA.
- Code copied from other projects without proper attribution and license compatibility.
- Large unrelated changes bundled into a single pull request.
- Changes that break the build (`build.bat` must still work).

---

## How to Submit a Contribution

1. **Sign the CLA** as described above. Wait for confirmation email.
2. **Fork** the repository: https://github.com/RedstoneShell/Depressure
3. **Create a branch** for your change:
```text
git checkout -b feature/my-new-driver
```

4. **Make your changes** following the guidelines above.
5. **Test your changes** by building with `build.bat` and, if possible, running Depressure in a UEFI environment (OVFM, QEMU with OVMF, or real hardware).
6. **Commit and push** your branch.
7. **Open a Pull Request** against the `main` branch of `RedstoneShell/Depressure`.
8. In the pull request description, mention that you have signed the CLA and include the email address you sent it from (so we can match it).

---

## Questions

If you have any questions about the CLA, the contribution process, or the project in general, contact:

> **depressuresupport@gmail.com**

Please do not send CLA-related questions to public GitHub issues, since they may involve personal information.

---

## Thank You

Depressure is built by contributors like you. Whether you fix a typo, write a driver, or improve the kernel - thank you for helping the project grow.
