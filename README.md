QuecAlpha doc website
=====

## Build locally

* Environment setup


    # Set up the development environment according to the link.
    https://developer.quectel.com/doc/quecpi/QuecPi-Alpha/zh/development-guide/develop-environment.html
  

## Commit and push

* Configure committing template

    ```bash
    # The configuration only takes effect in the current repository.
    # The command below should be executed everytime you clone this repository.
    git config commit.template ./commit.template

    # The configuration takes effect globally.
    # The command can ONLY be executed once (remember to copy commit.template file to a fixed path)
    git config --global commit.template path/to/commit.template
    ```

* Configure git editor

    ```bash
     # Configure VSCode as git editor
    git config --global core.editor "code --wait"

    # Configure vim as git editor
    git config --global core.editor vim
    ```

    > Choose one you are familiar with.

* Configure user.name & user.email

    ```bash
    # Use your full English name to replace <name>
    git config --global user.name <name>
    git config --global user.email <name>@quectel.com
    ```

* Check the status of project repository

    ```bash
    git status
    ```

* Add necessary files or directories those to be committed

    ```bash
    git add <files | dirs>
    ```

* Commit your modification

    ```bash
    # MUST NOT use '-m' option to append message directly!
    git commit
    ```

    > After this action, write your committing message in a pop-up editor window, and SAVE it before closing.

* Push your committing

    ```bash
    git push
    ```

* Create pull-request

Create pull-request according to pictures below:

![](./static/image/pr-button.png)

![](./static/image/create-pr.png)

## Note

* If you use `git commit -m` to commit accidentally, and havn't do a new committing, you can use `git commit --amend` to re-edit your committing message, and SAVE it before closing.

    > If you want to modify committing message that behind the newest one, please contact Chavis, because it is a bit complicated, you may don't know how to do it.
