PYTHON ?= python3
.PHONY: verify-week1 verify-week2 verify-week3 verify-week4 verify-week5 verify-feedback verify-handoff-contract verify-all
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
verify-feedback:
	$(PYTHON) scripts/verify-feedback.py
verify-all: verify-week1 verify-week2 verify-week3 verify-week4 verify-week5 verify-feedback
