# Start by checking out the tag we were provided.

git fetch
git checkout ${TAG} 2>&1 | tee checkout-output.log
if grep -q "^Switched to branch" checkout-output.log; then
    git pull
fi

git checkout .

git submodule init
git submodule update

git clean -f -f -x -d
