# Convenience wrappers; every target is a plain command you can run by hand.
PY      := .venv/bin/python
BUILD   := build
export PYTHONPATH := sim:.

.PHONY: demo chart all deps build test test-c test-py run compare train report decode proto clean

all: build

deps:                 ## fetch FreeRTOS/nanopb/Unity, create .venv
	scripts/bootstrap.sh

build:                ## configure + compile firmware, host tests and tools
	.venv/bin/cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	.venv/bin/cmake --build $(BUILD) -j

demo: build           ## replay a synthetic drive: events + projected battery life
	$(PY) tools/demo.py

chart: build          ## regenerate docs/battery_life.svg
	mkdir -p out
	$(PY) tools/compare_designs.py --duration 600 --json out/compare.json
	$(PY) tools/plot_battery.py out/compare.json docs/battery_life.svg

test: test-c test-py  ## all automated tests

test-c: build
	cd $(BUILD) && ../.venv/bin/ctest --output-on-failure

test-py: build
	$(PY) -m pytest -q

run: build            ## 10 simulated minutes, then decode the log
	mkdir -p out
	$(BUILD)/microamp --design wom --duration 600 --log out/microamp.log \
	    --report out/report.json --python $(CURDIR)/.venv/bin/python
	$(PY) tools/decode_log.py out/microamp.log

compare: build        ## polling vs fifo vs wom: battery life + detection quality
	$(PY) tools/compare_designs.py --duration 600

report: build         ## model size / cycle report
	$(BUILD)/ml_bench
	size $(BUILD)/CMakeFiles/fwcore.dir/firmware/ml/*.o $(BUILD)/CMakeFiles/fwcore.dir/firmware/model/*.o

train:                ## retrain the int8 model and regenerate C arrays + golden vectors
	$(PY) -m ml.train

proto:                ## regenerate nanopb sources
	scripts/gen_proto.sh

clean:
	rm -rf $(BUILD) out
