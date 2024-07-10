/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2023 Victor Kirhenshtein
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.ui.eclipse.networkmaps.dialogs;

import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;
import org.netxms.client.objects.AbstractObject;
import org.netxms.client.objects.NetworkMap;
import org.netxms.ui.eclipse.networkmaps.Messages;
import org.netxms.ui.eclipse.objectbrowser.dialogs.ObjectSelectionDialog;
import org.netxms.ui.eclipse.objectbrowser.widgets.ObjectSelector;
import org.netxms.ui.eclipse.tools.MessageDialogHelper;
import org.netxms.ui.eclipse.tools.WidgetHelper;

/**
 * Dialog for creating new network map object
 */
public class CreateNetworkMapDialog extends Dialog
{
	private Text textName;
   private Text textAlias;
	private Combo mapType;
	private ObjectSelector seedObjectSelector;
	private String name;
   private String alias;
	private int type;
	private long seedObject;

	/**
	 * @param parentShell
	 */
	public CreateNetworkMapDialog(Shell parentShell)
	{
		super(parentShell);
	}

   /**
    * @see org.eclipse.jface.window.Window#configureShell(org.eclipse.swt.widgets.Shell)
    */
	@Override
	protected void configureShell(Shell newShell)
	{
		super.configureShell(newShell);
		newShell.setText(Messages.get().CreateNetworkMapDialog_Title);
	}

   /**
    * @see org.eclipse.jface.dialogs.Dialog#createDialogArea(org.eclipse.swt.widgets.Composite)
    */
	@Override
	protected Control createDialogArea(Composite parent)
	{
		Composite dialogArea = (Composite)super.createDialogArea(parent);

		GridLayout layout = new GridLayout();
      layout.marginWidth = WidgetHelper.DIALOG_WIDTH_MARGIN;
      layout.marginHeight = WidgetHelper.DIALOG_HEIGHT_MARGIN;
      dialogArea.setLayout(layout);

      textName = WidgetHelper.createLabeledText(dialogArea, SWT.SINGLE | SWT.BORDER, SWT.DEFAULT, Messages.get().CreateNetworkMapDialog_Name, "", //$NON-NLS-1$
                                                WidgetHelper.DEFAULT_LAYOUT_DATA);
      textName.getShell().setMinimumSize(300, 0);

      textAlias = WidgetHelper.createLabeledText(dialogArea, SWT.SINGLE | SWT.BORDER, SWT.DEFAULT, Messages.get().CreateNetworkMapDialog_Alias, "", //$NON-NLS-1$
            WidgetHelper.DEFAULT_LAYOUT_DATA);
      textAlias.getShell().setMinimumSize(300, 0);

      mapType = WidgetHelper.createLabeledCombo(dialogArea, SWT.READ_ONLY, Messages.get().CreateNetworkMapDialog_MapType, WidgetHelper.DEFAULT_LAYOUT_DATA);
      mapType.add(Messages.get().CreateNetworkMapDialog_Custom);
      mapType.add(Messages.get().CreateNetworkMapDialog_L2Topology);
      mapType.add(Messages.get().CreateNetworkMapDialog_IpTopology);
      mapType.add("Internal Communication Topology");
      mapType.add("OSPF Topology");
      mapType.add("Hybrid Topology");
      mapType.select(0);
      GridData gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      mapType.getParent().setLayoutData(gd);
      mapType.addSelectionListener(new SelectionAdapter() {
			@Override
			public void widgetSelected(SelectionEvent e)
			{
		      seedObjectSelector.setEnabled(mapType.getSelectionIndex() > 0 && mapType.getSelectionIndex() != 3);
			}
      });

      seedObjectSelector = new ObjectSelector(dialogArea, SWT.NONE, true);
      seedObjectSelector.setLabel(Messages.get().CreateNetworkMapDialog_SeedNode);
      seedObjectSelector.setObjectClass(AbstractObject.class);
      seedObjectSelector.setClassFilter(ObjectSelectionDialog.createNodeSelectionFilter(false));
      seedObjectSelector.setEnabled(false);
      gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      seedObjectSelector.setLayoutData(gd);

		return dialogArea;
	}

   /**
    * @see org.eclipse.jface.dialogs.Dialog#okPressed()
    */
	@Override
	protected void okPressed()
	{
		name = textName.getText().trim();
      alias = textAlias.getText().trim();
		if (name.isEmpty())
		{
			MessageDialogHelper.openWarning(getShell(), Messages.get().CreateNetworkMapDialog_Warning, Messages.get().CreateNetworkMapDialog_PleaseEnterName);
			return;
		}

		type = mapType.getSelectionIndex();
      if ((type != NetworkMap.TYPE_CUSTOM) && (type != NetworkMap.TYPE_INTERNAL_TOPOLOGY))
		{
			seedObject = seedObjectSelector.getObjectId();
			if (seedObject == 0)
			{
				MessageDialogHelper.openWarning(getShell(), Messages.get().CreateNetworkMapDialog_Warning, Messages.get().CreateNetworkMapDialog_PleaseSelectSeed);
				return;
			}
		}

		super.okPressed();
	}

	/**
	 * @return the name
	 */
	public String getName()
	{
		return name;
	}

   /**
    * @return the alias
    */
   public String getAlias()
   {
      return alias;
   }

	/**
	 * @return the type
	 */
	public int getType()
	{
		return type;
	}

	/**
	 * @return the seedObject
	 */
	public long getSeedObject()
	{
		return seedObject;
	}
}
