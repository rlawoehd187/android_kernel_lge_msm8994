#
# Makefile for nfc devices
#

obj-$(CONFIG_LGE_NFC_PN544_C2)	:= pn544_lge.o
obj-$(CONFIG_LGE_NFC_PN544_C2)	+= pn544_lge_hwadapter.o
obj-$(CONFIG_LGE_NFC_PN544_C3)	:= pn544_lge.o
obj-$(CONFIG_LGE_NFC_PN544_C3)	+= pn544_lge_hwadapter.o
obj-$(CONFIG_LGE_NFC_PN547_C2)	:= pn547_lge.o
obj-$(CONFIG_LGE_NFC_PN547_C2)	+= pn547_lge_hwadapter.o
obj-$(CONFIG_NFC_PN544)		+= pn544/
obj-$(CONFIG_NFC_MICROREAD)	+= microread/
obj-$(CONFIG_NFC_PN533)		+= pn533.o
obj-$(CONFIG_NFC_WILINK)	+= nfcwilink.o
obj-$(CONFIG_NFC_MEI_PHY)	+= mei_phy.o
obj-$(CONFIG_NFC_QNCI)		+= nfc-nci.o
obj-$(CONFIG_NFC_NQ)		+= nq-nci.o

ccflags-$(CONFIG_NFC_DEBUG) := -DDEBUG
