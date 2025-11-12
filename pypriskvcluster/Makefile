PYBIND11_STUBGEN := $(shell command -v pybind11-stubgen 2> /dev/null)

.PHONY: all clean

PY_FILES := $(shell find . -type f -name "*.py")
CPP_FILES := $(shell find . -type f -name "*.cpp")

all: $(CPP_FILES) $(PY_FILES)
	python3 setup.py bdist_wheel

ifdef PYBIND11_STUBGEN
	PYTHONPATH=priskvcluster/_priskvcluster/ pybind11-stubgen -o priskvcluster/_priskvcluster/ _priskvcluster
endif

rebuild: clean
	make all

clean:
	find . -type f -name "*.so" -delete
	find . -type f -name "*.pyi" -delete
	rm -rf build dist priskvcluster.egg-info
