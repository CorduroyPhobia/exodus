#!/usr/bin/env bash
set -euo pipefail

source "$HOME/.sdkman/bin/sdkman-init.sh"

if ! sdk list java | grep -q '25-open'; then
  echo 'SDKMAN does not list 25-open; trying Temurin 25.'
fi
if yes | sdk install java 25-open; then
  sdk use java 25-open
else
  yes | sdk install java 25-tem
  sdk use java 25-tem
fi

yes | sdk install gradle 9.5.1 || true
sdk use gradle 9.5.1

java -version
gradle --version

rm -rf project
mkdir project
unzip -q project.zip -d project

cd project
gradle clean build --stacktrace --warning-mode all

version="${VERSION:-$(git rev-parse --short HEAD)}"
destination="$HOME/.m2/repository/com/github/CorduroyPhobia/exodus/$version"
mkdir -p "$destination"

jar_file="$(find build/libs -maxdepth 1 -type f -name '*.jar' ! -name '*-sources.jar' ! -name '*-dev.jar' | head -n 1)"
if [[ -z "$jar_file" ]]; then
  echo 'No main JAR was produced.' >&2
  exit 1
fi
cp "$jar_file" "$destination/exodus-$version.jar"

source_file="$(find build/libs -maxdepth 1 -type f -name '*-sources.jar' | head -n 1 || true)"
if [[ -n "$source_file" ]]; then
  cp "$source_file" "$destination/exodus-$version-sources.jar"
fi

cat > "$destination/exodus-$version.pom" <<POM
<project xmlns="http://maven.apache.org/POM/4.0.0" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 https://maven.apache.org/xsd/maven-4.0.0.xsd">
  <modelVersion>4.0.0</modelVersion>
  <groupId>com.github.CorduroyPhobia</groupId>
  <artifactId>exodus</artifactId>
  <version>$version</version>
  <packaging>jar</packaging>
</project>
POM
