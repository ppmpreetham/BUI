// A to-do list, MyGo style: a text input that adds on Return, check boxes
// bound to the items, keyed rows with a delete button each, and settings in a
// Blender panel.
//
//	go run ./examples/todos
//
// SPDX-License-Identifier: GPL-2.0-or-later
package main

import (
	"log"
	"sort"
	"strings"

	"github.com/ppmpreetham/BUI/bindings/go/bui"
	"github.com/ppmpreetham/BUI/bindings/go/ui"
)

type todo struct {
	id    int
	title string
	done  bool
}

// todoList is the app's state, which lasts.
type todoList struct {
	todos       []todo
	draft       string
	nextID      int
	showDone    bool
	sortBy      string
	optionsOpen bool
	order       []int // shown's, reused
}

func (app *todoList) add() {
	if app.draft == "" {
		return
	}
	app.nextID++
	app.todos = append(app.todos, todo{id: app.nextID, title: app.draft})
	app.draft = ""
}

func (app *todoList) remove(id int) {
	for i, t := range app.todos {
		if t.id == id {
			app.todos = append(app.todos[:i], app.todos[i+1:]...)
			return
		}
	}
}

// shown is the order the rows show in: as added, or by title. The rows keep
// their keys, so sorting moves them rather than making new ones.
func (app *todoList) shown() []int {
	app.order = app.order[:0]
	for i := range app.todos {
		app.order = append(app.order, i)
	}
	if app.sortBy == "Title" {
		sort.SliceStable(app.order, func(a, b int) bool {
			return strings.ToLower(app.todos[app.order[a]].title) < strings.ToLower(app.todos[app.order[b]].title)
		})
	}
	return app.order
}

// view builds the interface from the state.
func (app *todoList) view(c *ui.Context) {
	t := c.Theme()
	ui.Column(c).Fill().Padding(16).Gap(8).Children(func() {
		ui.Text(c, "To-dos").FontSize(20).Bold()
		ui.Row(c).Gap(6).Children(func() {
			if ui.TextInput(c.Key("draft"), &app.draft).Placeholder("New to-do").Grow(1).Submitted() {
				app.add()
			}
			ui.PrimaryButton(c.Key("add"), "Add").Disabled(app.draft == "").OnClick(app.add)
		})
		left := 0
		ui.Scroll(c).Grow(1).Children(func() {
			ui.Column(c).Gap(2).Children(func() {
				for _, i := range app.shown() {
					item := &app.todos[i]
					if !item.done {
						left++
					}
					if item.done && !app.showDone {
						continue
					}
					ui.Row(c.Key(item.id)).Gap(6).Children(func() {
						ui.Checkbox(c, &item.done, item.title).Grow(1)
						id := item.id
						ui.Button(c, "Delete").OnClick(func() { app.remove(id) })
					})
				}
			})
		})
		ui.Collapsible(c, "Options", &app.optionsOpen, func() {
			ui.Column(c).Padding(4).Gap(4).Children(func() {
				ui.Checkbox(c, &app.showDone, "Show done")
				ui.Select(c, &app.sortBy, []string{"Added", "Title"}).Label("Sort")
			})
		})
		ui.Textf(c, "%d left", left).TextColor(t.TextMuted)
	})
}

func main() {
	app := &todoList{showDone: true, sortBy: "Added"}
	for _, title := range []string{"Port the counter", "Write the docs", "Ship it"} {
		app.draft = title
		app.add()
	}
	app.todos[0].done = true
	bui.App.WhenReady(func() {
		if bui.NewWindow(bui.WindowOptions{Title: "To-dos", Width: 380, Height: 420, Content: ui.View(app.view)}) == nil {
			log.Fatal("bui: the window could not be made")
		}
	})
	if err := bui.App.Run(); err != nil {
		log.Fatal(err)
	}
}
