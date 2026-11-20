// Command kitgen builds BUI's C kit for the Go binding: `go generate ./bui`
// runs it. It configures CMake once in .bui/build with the C compiler cgo uses
// (`go env CC`, so the libraries link with it: MinGW gcc on Windows), builds
// bui_kit, and copies the static libraries to .bui/lib and the public headers to
// .bui/include, where package bui's #cgo lines look. Files are copied only when
// their content changed, so an up-to-date kit does not make Go rebuild the
// package.
//
// BUI_SOURCE_DIR builds another BUI checkout; BUI_CMAKE_GENERATOR picks another
// CMake generator (Ninja is used when it is on PATH).
//
// SPDX-License-Identifier: GPL-2.0-or-later
package main

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

func main() {
	if err := build(); err != nil {
		fmt.Fprintln(os.Stderr, "kitgen:", err)
		os.Exit(1)
	}
}

func build() error {
	module, err := moduleDir()
	if err != nil {
		return err
	}
	root := os.Getenv("BUI_SOURCE_DIR")
	if root == "" {
		root = filepath.Join(module, "..", "..")
	}
	if _, err := os.Stat(filepath.Join(root, "kit", "include", "BUI_kit.h")); err != nil {
		return fmt.Errorf("BUI source tree not found at %q (set BUI_SOURCE_DIR)", root)
	}
	out := filepath.Join(module, ".bui")
	dir := filepath.Join(out, "build")
	if _, err := os.Stat(filepath.Join(dir, "CMakeCache.txt")); err != nil {
		if err := configure(root, dir); err != nil {
			return err
		}
	}
	if err := run("cmake", "--build", dir, "--config", "Release", "--target", "bui_kit"); err != nil {
		return err
	}
	for _, lib := range []string{"kit", "gpu", "core"} {
		name := "libbui_" + lib + ".a"
		src := filepath.Join(dir, lib, name)
		if _, err := os.Stat(src); err != nil {
			src = filepath.Join(dir, lib, "Release", name)
		}
		if err := copyIfChanged(src, filepath.Join(out, "lib", name)); err != nil {
			return err
		}
	}
	for _, inc := range []string{"kit/include", "gpu/include"} {
		headers, _ := filepath.Glob(filepath.Join(root, inc, "*.h"))
		for _, h := range headers {
			if err := copyIfChanged(h, filepath.Join(out, "include", filepath.Base(h))); err != nil {
				return err
			}
		}
	}
	return nil
}

// configure runs CMake with cgo's compiler, Release, and only the kit.
func configure(root, dir string) error {
	cc := goEnv("CC")
	if cc == "" {
		cc = "gcc"
	}
	cxx := goEnv("CXX")
	if cxx == "" {
		cxx = "g++"
		if strings.Contains(cc, "clang") {
			cxx = "clang++"
		}
	}
	args := []string{"-S", root, "-B", dir, "-DCMAKE_BUILD_TYPE=Release"}
	// A launcher in front of the compiler ("ccache gcc") goes to CMake apart.
	for _, c := range []struct{ lang, cmd string }{{"C", cc}, {"CXX", cxx}} {
		words := strings.Fields(c.cmd)
		if len(words) > 1 {
			args = append(args, "-DCMAKE_"+c.lang+"_COMPILER_LAUNCHER="+words[0])
			words = words[1:]
		}
		args = append(args, "-DCMAKE_"+c.lang+"_COMPILER="+strings.Join(words, " "))
	}
	args = append(args,
		"-DBUI_BUILD_TESTS=OFF",
		"-DBUI_BUILD_EXAMPLES=OFF",
		"-DBUI_BUILD_PYTHON_BINDINGS=OFF",
		"-DBUI_BUILD_LUA_BINDINGS=OFF",
		"-DBUI_BUILD_QT_BACKEND=OFF",
		"-DBUI_BUILD_IMGUI=OFF",
	)
	gen := os.Getenv("BUI_CMAKE_GENERATOR")
	if gen == "" {
		if _, err := exec.LookPath("ninja"); err == nil {
			gen = "Ninja"
		} else if runtime.GOOS == "windows" {
			gen = "MinGW Makefiles"
		}
	}
	if gen != "" {
		args = append(args, "-G", gen)
	}
	return run("cmake", args...)
}

// moduleDir is the directory holding go.mod, from the working directory up
// (go generate runs in the package's directory).
func moduleDir() (string, error) {
	dir, err := os.Getwd()
	if err != nil {
		return "", err
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			return dir, nil
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", fmt.Errorf("go.mod not found above the working directory")
		}
		dir = parent
	}
}

func goEnv(key string) string {
	out, err := exec.Command("go", "env", key).Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

func run(name string, args ...string) error {
	cmd := exec.Command(name, args...)
	cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%s: %w", name, err)
	}
	return nil
}

func copyIfChanged(src, dst string) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	if old, err := os.ReadFile(dst); err == nil && bytes.Equal(old, data) {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	return os.WriteFile(dst, data, 0o644)
}
