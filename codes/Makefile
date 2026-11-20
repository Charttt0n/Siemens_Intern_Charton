PYTHON ?= python3
.PHONY: verify-week1 verify-week2 verify-week3 verify-week4 verify-week5 verify-week6 verify-feedback verify-handoff-contract verify-all
verify-week1:
	$(PYTHON) scripts/verify-startup.py
verify-week2:
	$(PYTHON) scripts/verify-week2.py
verify-week3:
	$(PYTHON) scripts/verify-week3.py
verify-week4:
	$(PYTHON) scripts/verify-week4.py
verify-handoff-contract:
	$(PYTHON) scripts/verify-handoff-contract.py
# Full Week-5 code acceptance, including controlled NOR tears and restart.
verify-week5:
	$(PYTHON) scripts/verify-week5.py
# Week 6 is a separate entry; verify-all keeps the Week 1-5 contract.
verify-week6:
	$(PYTHON) scripts/verify-week6.py
.PHONY: verify-week7 benchmark-week7 debug-week7
verify-week7:
	$(PYTHON) scripts/verify-week7.py
benchmark-week7:
	bash scripts/build-week7.sh
	$(PYTHON) scripts/benchmark-week7.py
debug-week7:
	bash scripts/build-week7.sh
	$(PYTHON) scripts/debug-week7.py --out logs/week7-debug-local
verify-feedback:
	$(PYTHON) scripts/verify-feedback.py
verify-all: verify-week1 verify-week2 verify-week3 verify-week4 verify-week5 verify-feedback
