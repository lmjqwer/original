.PHONY: rf_fcc_drv rf_pdt_drv rf_mp_drv clean

all: rf_fcc_drv rf_pdt_drv rf_mp_drv

rf_fcc_drv:
	cd ./rf_fcc_drv && make || exit "$$?"

rf_pdt_drv:
	cd ./rf_pdt_drv && make || exit "$$?"

rf_mp_drv:
	cd ./rf_mp_drv && make || exit "$$?"

clean:
	cd ./rf_fcc_drv && make clean
	cd ./rf_pdt_drv && make clean
	cd ./rf_mp_drv && make clean
