obj-${CONFIG_MEDIA} += media_player.o
obj-${CONFIG_MEDIA} += media_resample.o
obj-${CONFIG_MEDIA} += okmic_player.o
ifdef CONFIG_MEDIA
ifdef CONFIG_USOUND_MEETING_MODE
obj-y += media_mem_usound.o
else
ifdef CONFIG_TWS
obj-y += media_mem.o
else
obj-y += media_mem_no_tws.o
endif
endif
endif

obj-${CONFIG_MEDIA} += media_effect_param.o
obj-${CONFIG_USER_MUSIC_EFFECT} += user_audio_processor/